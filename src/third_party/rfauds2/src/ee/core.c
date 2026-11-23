#include <rfauds2/rfauds2.h>

#define RFAUDS2_Q15_ONE 32768
#define RFAUDS2_Q16_ONE 65536

#include "sinc8_table.h"

static u64 divide_u64_u32(u64 numerator, u32 denominator)
{
    u64 quotient = 0;
    u64 remainder = 0;
    s32 bit;

    if (denominator == 0)
        return 0;

    for (bit = 63; bit >= 0; --bit) {
        remainder =
            (remainder << 1) |
            ((numerator >> (u32)bit) & 1u);

        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= (u64)1u << (u32)bit;
        }
    }

    return quotient;
}

static s16 saturate_s16(s64 value)
{
    if (value > 32767)
        return 32767;

    if (value < -32768)
        return -32768;

    return (s16)value;
}

/*
 * Four-point forward Lagrange interpolation.
 *
 * Unlike a Catmull-Rom form, this needs no hidden look-behind state: the
 * caller simply keeps the three unconsumed source frames reported by the
 * converter.  Coefficients are evaluated in Q16 and the final sample is
 * saturated to S16.
 */
static s16 cubic_lagrange_s16(
    s32 p0,
    s32 p1,
    s32 p2,
    s32 p3,
    u32 fraction)
{
    s64 t = (s64)(fraction >> 16);
    s64 tm1 = t - RFAUDS2_Q16_ONE;
    s64 tm2 = t - 2 * RFAUDS2_Q16_ONE;
    s64 tm3 = t - 3 * RFAUDS2_Q16_ONE;
    s64 c0;
    s64 c1;
    s64 c2;
    s64 c3;
    s64 value;

    /* Each triple product is Q48; >>32 leaves a Q16 coefficient. */
    c0 = -(((tm1 * tm2 * tm3) >> 32) / 6);
    c1 =  (((t   * tm2 * tm3) >> 32) / 2);
    c2 = -(((t   * tm1 * tm3) >> 32) / 2);
    c3 =  (((t   * tm1 * tm2) >> 32) / 6);

    value =
        (s64)p0 * c0 +
        (s64)p1 * c1 +
        (s64)p2 * c2 +
        (s64)p3 * c3;

    if (value >= 0)
        value = (value + RFAUDS2_Q16_ONE / 2) >> 16;
    else
        value = -(((-value) + RFAUDS2_Q16_ONE / 2) >> 16);

    return saturate_s16(value);
}

int rfauds2_rate_converter_init(
    rfauds2_rate_converter *converter,
    u32 input_rate,
    u32 output_rate,
    u8 channels,
    rfauds2_resample_mode mode)
{
    u64 numerator;

    if (converter == 0)
        return -1;

    if (input_rate == 0 || output_rate == 0)
        return -2;

    if (channels != 1 && channels != 2)
        return -3;

    if (mode != RFAUDS2_RESAMPLE_NEAREST &&
        mode != RFAUDS2_RESAMPLE_LINEAR &&
        mode != RFAUDS2_RESAMPLE_CUBIC &&
        mode != RFAUDS2_RESAMPLE_SINC8)
        return -4;

    /*
     * This first Sinc8 path is an interpolation/up-sampling kernel.  Refuse
     * down-sampling rather than pretending to provide an anti-alias low-pass
     * that it does not yet implement.
     */
    if (mode == RFAUDS2_RESAMPLE_SINC8 &&
        input_rate > output_rate)
        return -6;

    converter->input_rate = input_rate;
    converter->output_rate = output_rate;
    converter->channels = channels;
    converter->mode = mode;
    converter->phase_q32 = 0;

    numerator = (u64)input_rate << 32;
    converter->step_q32 =
        divide_u64_u32(numerator, output_rate);

    if (converter->step_q32 == 0)
        return -5;

    return 0;
}

void rfauds2_rate_converter_reset(
    rfauds2_rate_converter *converter)
{
    if (converter != 0)
        converter->phase_q32 = 0;
}

u32 rfauds2_rate_converter_process_s16(
    rfauds2_rate_converter *converter,
    const s16 *input,
    u32 input_frames,
    s16 *output,
    u32 output_frames_capacity,
    u32 *input_frames_consumed)
{
    u32 produced = 0;
    u32 channels;
    u64 phase;

    if (input_frames_consumed != 0)
        *input_frames_consumed = 0;

    if (converter == 0 ||
        input == 0 ||
        output == 0 ||
        input_frames_consumed == 0 ||
        output_frames_capacity == 0 ||
        input_frames == 0)
        return 0;

    channels = converter->channels;

    if (converter->input_rate == converter->output_rate) {
        u32 frames = input_frames;
        u32 sample_count;
        u32 i;

        if (frames > output_frames_capacity)
            frames = output_frames_capacity;

        sample_count = frames * channels;

        for (i = 0; i < sample_count; ++i)
            output[i] = input[i];

        converter->phase_q32 = 0;
        *input_frames_consumed = frames;
        return frames;
    }

    if (converter->mode == RFAUDS2_RESAMPLE_CUBIC) {
        if (input_frames < 4)
            return 0;
    } else if (converter->mode == RFAUDS2_RESAMPLE_SINC8) {
        if (input_frames < 5)
            return 0;
    } else if (input_frames < 2) {
        return 0;
    }

    phase = converter->phase_q32;

    while (produced < output_frames_capacity) {
        u32 input_index = (u32)(phase >> 32);
        u32 fraction = (u32)phase;
        u32 channel;

        if (converter->mode == RFAUDS2_RESAMPLE_CUBIC) {
            if (input_index + 3u >= input_frames)
                break;
        } else if (converter->mode == RFAUDS2_RESAMPLE_SINC8) {
            if (input_index + 4u >= input_frames)
                break;
        } else if (input_index + 1u >= input_frames) {
            break;
        }

        for (channel = 0; channel < channels; ++channel) {
            s32 a = input[input_index * channels + channel];
            s32 b = input[(input_index + 1u) * channels + channel];
            s32 sample;

            if (converter->mode == RFAUDS2_RESAMPLE_NEAREST) {
                sample =
                    (fraction < 0x80000000u) ? a : b;
            } else if (converter->mode == RFAUDS2_RESAMPLE_LINEAR) {
                u32 fraction_q16 = fraction >> 16;
                s32 delta = b - a;
                s64 interpolated =
                    (s64)delta * (s64)fraction_q16;

                sample =
                    a + (s32)(interpolated >> 16);
            } else if (converter->mode == RFAUDS2_RESAMPLE_CUBIC) {
                s32 p2 =
                    input[(input_index + 2u) * channels + channel];
                s32 p3 =
                    input[(input_index + 3u) * channels + channel];

                sample = cubic_lagrange_s16(
                    a,
                    b,
                    p2,
                    p3,
                    fraction);
            } else {
                u32 phase_index = fraction >> 24;
                s64 sum = 0;
                s32 tap;

                for (tap = 0; tap < 8; ++tap) {
                    s32 source_index =
                        (s32)input_index + tap - 3;

                    if (source_index < 0)
                        source_index = 0;

                    sum +=
                        (s64)input[
                            (u32)source_index * channels + channel] *
                        (s64)rfauds2_sinc8_q15[phase_index][tap];
                }

                if (sum >= 0)
                    sum = (sum + (RFAUDS2_Q15_ONE / 2)) >> 15;
                else
                    sum = -(((-sum) + (RFAUDS2_Q15_ONE / 2)) >> 15);

                sample = saturate_s16(sum);
            }

            output[produced * channels + channel] =
                (s16)sample;
        }

        ++produced;
        phase += converter->step_q32;
    }

    {
        u64 consumed64 = phase >> 32;
        u32 consumed;

        if (converter->mode == RFAUDS2_RESAMPLE_SINC8) {
            /*
             * Keep three frames behind floor(phase) so the next chunk has
             * the negative side of the 8-tap window without hidden state.
             */
            if (consumed64 > 3u)
                consumed = (u32)(consumed64 - 3u);
            else
                consumed = 0;

            if (consumed > input_frames)
                consumed = input_frames;
        } else {
            u32 retain =
                (converter->mode == RFAUDS2_RESAMPLE_CUBIC) ? 3u : 1u;
            u32 max_consumed = input_frames - retain;

            if (consumed64 > max_consumed)
                consumed = max_consumed;
            else
                consumed = (u32)consumed64;
        }

        phase -= (u64)consumed << 32;
        converter->phase_q32 = phase;
        *input_frames_consumed = consumed;
    }

    return produced;
}

void rfauds2_mix_s16(
    s16 *destination,
    const s16 *source,
    u32 sample_count,
    s32 gain_q15)
{
    u32 i;

    if (destination == 0 || source == 0)
        return;

    for (i = 0; i < sample_count; ++i) {
        s64 scaled =
            (s64)source[i] * (s64)gain_q15;
        s64 mixed;

        if (scaled >= 0)
            scaled =
                (scaled + RFAUDS2_Q15_ONE / 2) >> 15;
        else
            scaled =
                -(((-scaled) + RFAUDS2_Q15_ONE / 2) >> 15);

        mixed = (s64)destination[i] + scaled;
        destination[i] = saturate_s16(mixed);
    }
}


void rfauds2_bus_mixer_init(rfauds2_bus_mixer *mixer)
{
    u32 i;

    if (mixer == 0)
        return;

    for (i = 0; i < RFAUDS2_BUS_COUNT; ++i) {
        mixer->gain_q15[i] = RFAUDS2_Q15_ONE;
        mixer->muted[i] = 0;
    }
}

int rfauds2_bus_mixer_set_gain(
    rfauds2_bus_mixer *mixer,
    rfauds2_bus bus,
    s32 gain_q15)
{
    if (mixer == 0)
        return -1;

    if ((u32)bus >= RFAUDS2_BUS_COUNT)
        return -2;

    if (gain_q15 < 0)
        return -3;

    mixer->gain_q15[(u32)bus] = gain_q15;
    return 0;
}

int rfauds2_bus_mixer_set_mute(
    rfauds2_bus_mixer *mixer,
    rfauds2_bus bus,
    int muted)
{
    if (mixer == 0)
        return -1;

    if ((u32)bus >= RFAUDS2_BUS_COUNT)
        return -2;

    mixer->muted[(u32)bus] = muted ? 1u : 0u;
    return 0;
}

void rfauds2_bus_mixer_clear_s16(
    s16 *destination,
    u32 sample_count)
{
    u32 i;

    if (destination == 0)
        return;

    for (i = 0; i < sample_count; ++i)
        destination[i] = 0;
}

int rfauds2_bus_mixer_mix_s16(
    const rfauds2_bus_mixer *mixer,
    rfauds2_bus bus,
    s16 *destination,
    const s16 *source,
    u32 sample_count)
{
    if (mixer == 0 || destination == 0 || source == 0)
        return -1;

    if ((u32)bus >= RFAUDS2_BUS_COUNT)
        return -2;

    if (mixer->muted[(u32)bus])
        return 0;

    rfauds2_mix_s16(
        destination,
        source,
        sample_count,
        mixer->gain_q15[(u32)bus]);

    return 0;
}
