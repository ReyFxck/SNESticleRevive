/*
 * EE/R5900 stereo voice accumulator, o32 ABI.
 *
 * void SNSpcMixVoicePS2(int32_t *left, int32_t *right,
 *                        const int16_t *envelopedVoice, int32_t samples,
 *                        int32_t volumeLeft, int32_t volumeRight);
 *
 * The fifth and sixth arguments occupy 16(sp) and 20(sp). Only caller-saved
 * registers are used; the buffers are updated in-place with wrapping 32-bit
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
	blez	$a3, 2f
	nop
	lw	$t0, 16($sp)
	lw	$t1, 20($sp)
1:
	lh	$t2, 0($a2)
	lw	$t3, 0($a0)
	lw	$t5, 0($a1)
	mult	$t2, $t0
	addiu	$a2, $a2, 2
	mflo	$t4
	addu	$t4, $t4, $t3
	sw	$t4, 0($a0)
	mult	$t2, $t1
	addiu	$a0, $a0, 4
	mflo	$t4
	addu	$t4, $t4, $t5
	sw	$t4, 0($a1)
	addiu	$a1, $a1, 4
	addiu	$a3, $a3, -1
	bgtz	$a3, 1b
	nop
2:
	jr	$ra
	nop
	.end SNSpcMixVoicePS2
