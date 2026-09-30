/*
 * EE/R5900 stereo voice accumulator, o32 ABI.
 *
 * void SNSpcMixVoicePS2(int32_t *left, int32_t *right,
 *                        const int16_t *envelopedVoice, int32_t samples,
 *                        int32_t volumeLeft, int32_t volumeRight);
 *
 * The PS2 EE ABI passes the fifth and sixth integer arguments in GPR $8/$9.
 * Only caller-saved registers are used; the buffers are updated in-place with wrapping 32-bit
 * accumulation, just like the existing scalar mixer on the PS2.
 *
 * Inputs contain the already envelope-scaled/bit0-cleared DSP voice output,
 * so there is no second decodedPCM*envelope multiply in this loop.
 * Keep this conservative scalar-MIPS kernel separate from the existing
 * 128-bit MMI final mixer: that routine is sensitive to compiler scheduling.
 */
	.text
	.p2align 4
	.set noreorder
	.set noat
	.globl SNSpcMixVoicePS2
	.ent SNSpcMixVoicePS2
SNSpcMixVoicePS2:
	/* $4-$7 = a0-a3, $8/$9 = fifth/sixth integer args on the EE ABI. */
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
