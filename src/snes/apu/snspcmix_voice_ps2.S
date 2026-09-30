/*
 * EE/R5900 stereo voice accumulator, N32 ABI.
 *
 * void SNSpcMixVoicePS2(int32_t *left, int32_t *right,
 *                        const int16_t *pcm, const uint8_t *envelope,
 *                        int32_t samples, int32_t volumeLeft,
 *                        int32_t volumeRight);
 *
 * The PS2 EE N32 ABI passes these seven integer/pointer arguments in
 * GPR $4-$10. Only caller-saved registers are used.
 *
 * This deliberately preserves the historical Revive mixer math:
 *   mix = (pcm * envelope) >> 7;
 *   left  += mix * volumeLeft;
 *   right += mix * volumeRight;
 *
 * PMON/OUTX use a separately clamped/even S-DSP voice latch; do not feed
 * that latch into this mixer or the low output bits change.
 */
	.text
	.p2align 4
	.set noreorder
	.set noat
	.globl SNSpcMixVoicePS2
	.ent SNSpcMixVoicePS2
SNSpcMixVoicePS2:
	/* $4=L, $5=R, $6=PCM, $7=ENV, $8=count, $9=VL, $10=VR */
	blez	$8, 2f
	nop
1:
	lh	$11, 0($6)
	lbu	$12, 0($7)
	lw	$14, 0($4)
	lw	$15, 0($5)

	mult	$11, $12
	addiu	$6, $6, 2
	addiu	$7, $7, 1
	mflo	$13
	sra	$13, $13, 7

	mult	$13, $9
	nop
	mflo	$24
	addu	$24, $24, $14
	sw	$24, 0($4)

	mult	$13, $10
	addiu	$4, $4, 4
	mflo	$24
	addu	$24, $24, $15
	sw	$24, 0($5)

	addiu	$5, $5, 4
	addiu	$8, $8, -1
	bgtz	$8, 1b
	nop
2:
	jr	$31
	nop
	.end SNSpcMixVoicePS2
