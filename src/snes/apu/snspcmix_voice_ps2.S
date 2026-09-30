/*
 * EE/R5900 stereo voice accumulator, N32 ABI.
 *
 * void SNSpcMixVoicePS2(int32_t *left, int32_t *right,
 *                        const int16_t *voiceOutput, int32_t samples,
 *                        int32_t volumeLeft, int32_t volumeRight);
 *
 * $4-$9 carry the six integer/pointer arguments. voiceOutput is the exact
 * S-DSP voice latch (Gaussian/noise * 11-bit envelope, bit0 cleared), so the
 * hot EE loop only applies per-channel volume and accumulates.
 */
	.text
	.p2align 4
	.set noreorder
	.set noat
	.globl SNSpcMixVoicePS2
	.ent SNSpcMixVoicePS2
SNSpcMixVoicePS2:
	/* $4=L, $5=R, $6=voice, $7=count, $8=VL, $9=VR */
	blez	$7, 2f
	nop
1:
	lh	$10, 0($6)
	lw	$11, 0($4)
	lw	$12, 0($5)

	mult	$10, $8
	addiu	$6, $6, 2
	mflo	$13
	addu	$13, $13, $11
	sw	$13, 0($4)

	mult	$10, $9
	addiu	$4, $4, 4
	mflo	$13
	addu	$13, $13, $12
	sw	$13, 0($5)

	addiu	$5, $5, 4
	addiu	$7, $7, -1
	bgtz	$7, 1b
	nop
2:
	jr	$31
	nop
	.end SNSpcMixVoicePS2
