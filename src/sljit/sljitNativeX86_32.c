/*
 *    Stack-less Just-In-Time compiler
 *
 *    Copyright Zoltan Herczeg (hzmester@freemail.hu). All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice, this list of
 *      conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright notice, this list
 *      of conditions and the following disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* x86 32-bit arch dependent functions. */

/* --------------------------------------------------------------------- */
/*  Operators                                                            */
/* --------------------------------------------------------------------- */

static sljit_s32 emit_do_imm(struct sljit_compiler *compiler, sljit_u8 opcode, sljit_sw imm)
{
	sljit_u8 *inst;

	inst = (sljit_u8*)ensure_buf(compiler, 1 + 1 + sizeof(sljit_sw));
	FAIL_IF(!inst);
	INC_SIZE(1 + sizeof(sljit_sw));
	*inst++ = opcode;
	sljit_unaligned_store_sw(inst, imm);
	return SLJIT_SUCCESS;
}

/* Size contains the flags as well. */
static sljit_u8* emit_x86_instruction(struct sljit_compiler *compiler, sljit_uw size,
	/* The register or immediate operand. */
	sljit_s32 a, sljit_sw imma,
	/* The general operand (not immediate). */
	sljit_s32 b, sljit_sw immb)
{
	sljit_u8 *inst;
	sljit_u8 *buf_ptr;
	sljit_u8 reg_map_b;
	sljit_uw flags = size;
	sljit_uw inst_size;

	/* Both cannot be switched on. */
	SLJIT_ASSERT((flags & (EX86_BIN_INS | EX86_SHIFT_INS)) != (EX86_BIN_INS | EX86_SHIFT_INS));
	/* Size flags not allowed for typed instructions. */
	SLJIT_ASSERT(!(flags & (EX86_BIN_INS | EX86_SHIFT_INS)) || (flags & (EX86_BYTE_ARG | EX86_HALF_ARG)) == 0);
	/* Both size flags cannot be switched on. */
	SLJIT_ASSERT((flags & (EX86_BYTE_ARG | EX86_HALF_ARG)) != (EX86_BYTE_ARG | EX86_HALF_ARG));
	/* SSE2 and immediate is not possible. */
	SLJIT_ASSERT(!(a & SLJIT_IMM) || !(flags & EX86_SSE2));
	SLJIT_ASSERT((flags & (EX86_PREF_F2 | EX86_PREF_F3)) != (EX86_PREF_F2 | EX86_PREF_F3)
		&& (flags & (EX86_PREF_F2 | EX86_PREF_66)) != (EX86_PREF_F2 | EX86_PREF_66)
		&& (flags & (EX86_PREF_F3 | EX86_PREF_66)) != (EX86_PREF_F3 | EX86_PREF_66));

	size &= 0xf;
	inst_size = size;

	if (flags & (EX86_PREF_F2 | EX86_PREF_F3))
		inst_size++;
	if (flags & EX86_PREF_66)
		inst_size++;

	/* Calculate size of b. */
	inst_size += 1; /* mod r/m byte. */
	if (b & SLJIT_MEM) {
		if (!(b & REG_MASK))
			inst_size += sizeof(sljit_sw);
		else if (immb != 0 && !(b & OFFS_REG_MASK)) {
			/* Immediate operand. */
			if (immb <= 127 && immb >= -128)
				inst_size += sizeof(sljit_s8);
			else
				inst_size += sizeof(sljit_sw);
		}
		else if (reg_map[b & REG_MASK] == 5)
			inst_size += sizeof(sljit_s8);

		if ((b & REG_MASK) == SLJIT_SP && !(b & OFFS_REG_MASK))
			b |= TO_OFFS_REG(SLJIT_SP);

		if (b & OFFS_REG_MASK)
			inst_size += 1; /* SIB byte. */
	}

	/* Calculate size of a. */
	if (a & SLJIT_IMM) {
		if (flags & EX86_BIN_INS) {
			if (imma <= 127 && imma >= -128) {
				inst_size += 1;
				flags |= EX86_BYTE_ARG;
			} else
				inst_size += 4;
		}
		else if (flags & EX86_SHIFT_INS) {
			imma &= 0x1f;
			if (imma != 1) {
				inst_size ++;
				flags |= EX86_BYTE_ARG;
			}
		} else if (flags & EX86_BYTE_ARG)
			inst_size++;
		else if (flags & EX86_HALF_ARG)
			inst_size += sizeof(short);
		else
			inst_size += sizeof(sljit_sw);
	}
	else
		SLJIT_ASSERT(!(flags & EX86_SHIFT_INS) || a == SLJIT_PREF_SHIFT_REG);

	inst = (sljit_u8*)ensure_buf(compiler, 1 + inst_size);
	PTR_FAIL_IF(!inst);

	/* Encoding the byte. */
	INC_SIZE(inst_size);
	if (flags & EX86_PREF_F2)
		*inst++ = 0xf2;
	if (flags & EX86_PREF_F3)
		*inst++ = 0xf3;
	if (flags & EX86_PREF_66)
		*inst++ = 0x66;

	buf_ptr = inst + size;

	/* Encode mod/rm byte. */
	if (!(flags & EX86_SHIFT_INS)) {
		if ((flags & EX86_BIN_INS) && (a & SLJIT_IMM))
			*inst = (flags & EX86_BYTE_ARG) ? GROUP_BINARY_83 : GROUP_BINARY_81;

		if (a & SLJIT_IMM)
			*buf_ptr = 0;
		else if (!(flags & EX86_SSE2_OP1))
			*buf_ptr = U8(reg_map[a] << 3);
		else
			*buf_ptr = U8(a << 3);
	}
	else {
		if (a & SLJIT_IMM) {
			if (imma == 1)
				*inst = GROUP_SHIFT_1;
			else
				*inst = GROUP_SHIFT_N;
		} else
			*inst = GROUP_SHIFT_CL;
		*buf_ptr = 0;
	}

	if (!(b & SLJIT_MEM)) {
		*buf_ptr = U8(*buf_ptr | MOD_REG | (!(flags & EX86_SSE2_OP2) ? reg_map[b] : b));
		buf_ptr++;
	} else if (b & REG_MASK) {
		reg_map_b = reg_map[b & REG_MASK];

		if (!(b & OFFS_REG_MASK) || (b & OFFS_REG_MASK) == TO_OFFS_REG(SLJIT_SP) || reg_map_b == 5) {
			if (immb != 0 || reg_map_b == 5) {
				if (immb <= 127 && immb >= -128)
					*buf_ptr |= 0x40;
				else
					*buf_ptr |= 0x80;
			}

			if (!(b & OFFS_REG_MASK))
				*buf_ptr++ |= reg_map_b;
			else {
				*buf_ptr++ |= 0x04;
				*buf_ptr++ = U8(reg_map_b | (reg_map[OFFS_REG(b)] << 3));
			}

			if (immb != 0 || reg_map_b == 5) {
				if (immb <= 127 && immb >= -128)
					*buf_ptr++ = U8(immb); /* 8 bit displacement. */
				else {
					sljit_unaligned_store_sw(buf_ptr, immb); /* 32 bit displacement. */
					buf_ptr += sizeof(sljit_sw);
				}
			}
		}
		else {
			*buf_ptr++ |= 0x04;
			*buf_ptr++ = U8(reg_map_b | (reg_map[OFFS_REG(b)] << 3) | (immb << 6));
		}
	}
	else {
		*buf_ptr++ |= 0x05;
		sljit_unaligned_store_sw(buf_ptr, immb); /* 32 bit displacement. */
		buf_ptr += sizeof(sljit_sw);
	}

	if (a & SLJIT_IMM) {
		if (flags & EX86_BYTE_ARG)
			*buf_ptr = U8(imma);
		else if (flags & EX86_HALF_ARG)
			sljit_unaligned_store_s16(buf_ptr, (sljit_s16)imma);
		else if (!(flags & EX86_SHIFT_INS))
			sljit_unaligned_store_sw(buf_ptr, imma);
	}

	return !(flags & EX86_SHIFT_INS) ? inst : (inst + 1);
}

/* --------------------------------------------------------------------- */
/*  Enter / return                                                       */
/* --------------------------------------------------------------------- */

static sljit_u8* generate_far_jump_code(struct sljit_jump *jump, sljit_u8 *code_ptr, sljit_sw executable_offset)
{
	sljit_uw type = jump->flags >> TYPE_SHIFT;

	if (type == SLJIT_JUMP) {
		*code_ptr++ = JMP_i32;
		jump->addr++;
	}
	else if (type >= SLJIT_FAST_CALL) {
		*code_ptr++ = CALL_i32;
		jump->addr++;
	}
	else {
		*code_ptr++ = GROUP_0F;
		*code_ptr++ = get_jump_code(type);
		jump->addr += 2;
	}

	if (jump->flags & JUMP_LABEL)
		jump->flags |= PATCH_MW;
	else
		sljit_unaligned_store_sw(code_ptr, (sljit_sw)(jump->u.target - (jump->addr + 4) - (sljit_uw)executable_offset));
	code_ptr += 4;

	return code_ptr;
}

SLJIT_API_FUNC_ATTRIBUTE sljit_s32 sljit_emit_enter(struct sljit_compiler *compiler,
	sljit_s32 options, sljit_s32 arg_types, sljit_s32 scratches, sljit_s32 saveds,
	sljit_s32 fscratches, sljit_s32 fsaveds, sljit_s32 local_size)
{
	sljit_s32 word_arg_count, float_arg_count, args_size, types;
	sljit_uw size;
	sljit_u8 *inst;

	CHECK_ERROR();
	CHECK(check_sljit_emit_enter(compiler, options, arg_types, scratches, saveds, fscratches, fsaveds, local_size));
	set_emit_enter(compiler, options, arg_types, scratches, saveds, fscratches, fsaveds, local_size);

	/* Emit ENDBR32 at function entry if needed.  */
	FAIL_IF(emit_endbranch(compiler));

	SLJIT_COMPILE_ASSERT(SLJIT_FR0 == 1, float_register_index_start);

	arg_types >>= SLJIT_ARG_SHIFT;
	types = arg_types;
	word_arg_count = 0;
	float_arg_count = 0;
	args_size = SSIZE_OF(sw);
	while (types) {
		switch (types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			float_arg_count++;
			FAIL_IF(emit_sse2_load(compiler, 0, float_arg_count, SLJIT_MEM1(SLJIT_SP), args_size));
			args_size += SSIZE_OF(f64);
			break;
		case SLJIT_ARG_TYPE_F32:
			float_arg_count++;
			FAIL_IF(emit_sse2_load(compiler, 1, float_arg_count, SLJIT_MEM1(SLJIT_SP), args_size));
			args_size += SSIZE_OF(f32);
			break;
		default:
			word_arg_count++;
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
			if (word_arg_count > 2)
				args_size += SSIZE_OF(sw);
#else
			args_size += SSIZE_OF(sw);
#endif
			break;
		}
		types >>= SLJIT_ARG_SHIFT;
	}

	args_size -= SSIZE_OF(sw);
	compiler->args_size = args_size;

	/* [esp+0] for saving temporaries and function calls. */
	compiler->stack_tmp_size = 2 * SSIZE_OF(sw);

#if !(defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if (scratches > 3)
		compiler->stack_tmp_size = 3 * SSIZE_OF(sw);
#endif

	compiler->saveds_offset = compiler->stack_tmp_size;
	if (scratches > 3)
		compiler->saveds_offset += ((scratches > (3 + 6)) ? 6 : (scratches - 3)) * SSIZE_OF(sw);

	compiler->locals_offset = compiler->saveds_offset;

	if (saveds > 3)
		compiler->locals_offset += (saveds - 3) * SSIZE_OF(sw);

	if (options & SLJIT_F64_ALIGNMENT)
		compiler->locals_offset = (compiler->locals_offset + SSIZE_OF(f64) - 1) & ~(SSIZE_OF(f64) - 1);

	size = (sljit_uw)(1 + (scratches > 9 ? (scratches - 9) : 0) + (saveds <= 3 ? saveds : 3));
	inst = (sljit_u8*)ensure_buf(compiler, 1 + size);
	FAIL_IF(!inst);

	INC_SIZE(size);
	PUSH_REG(reg_map[TMP_REG1]);
	if (saveds > 2 || scratches > 9)
		PUSH_REG(reg_map[SLJIT_S2]);
	if (saveds > 1 || scratches > 10)
		PUSH_REG(reg_map[SLJIT_S1]);
	if (saveds > 0 || scratches > 11)
		PUSH_REG(reg_map[SLJIT_S0]);

	if (word_arg_count >= 4)
		EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), args_size + (sljit_s32)(size * sizeof(sljit_sw)));

	word_arg_count = 0;
	args_size = (sljit_s32)((size + 1) * sizeof(sljit_sw));
	while (arg_types) {
		switch (arg_types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			args_size += SSIZE_OF(f64);
			break;
		case SLJIT_ARG_TYPE_F32:
			args_size += SSIZE_OF(f32);
			break;
		default:
			word_arg_count++;
			if (word_arg_count <= 3) {
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
				if (word_arg_count <= 2)
					break;
#endif
				EMIT_MOV(compiler, SLJIT_S0 + 1 - word_arg_count, 0, SLJIT_MEM1(SLJIT_SP), args_size);
			}
			args_size += SSIZE_OF(sw);
			break;
		}
		arg_types >>= SLJIT_ARG_SHIFT;
	}

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if (word_arg_count > 0)
		EMIT_MOV(compiler, SLJIT_S0, 0, SLJIT_R2, 0);
	if (word_arg_count > 1)
		EMIT_MOV(compiler, SLJIT_S1, 0, SLJIT_R1, 0);
#endif

	SLJIT_ASSERT(SLJIT_LOCALS_OFFSET > 0);

#if defined(__APPLE__)
	/* Ignore pushed registers and SLJIT_LOCALS_OFFSET when computing the aligned local size. */
	saveds = (2 + (scratches > 9 ? (scratches - 9) : 0) + (saveds <= 3 ? saveds : 3)) * SSIZE_OF(sw);
	local_size = ((SLJIT_LOCALS_OFFSET + saveds + local_size + 15) & ~15) - saveds;
#else
	if (options & SLJIT_F64_ALIGNMENT)
		local_size = SLJIT_LOCALS_OFFSET + ((local_size + SSIZE_OF(f64) - 1) & ~(SSIZE_OF(f64) - 1));
	else
		local_size = SLJIT_LOCALS_OFFSET + ((local_size + SSIZE_OF(sw) - 1) & ~(SSIZE_OF(sw) - 1));
#endif

	compiler->local_size = local_size;

#ifdef _WIN32
	if (local_size > 0) {
		if (local_size <= 4 * 4096) {
			if (local_size > 4096)
				EMIT_MOV(compiler, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_SP), -4096);
			if (local_size > 2 * 4096)
				EMIT_MOV(compiler, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_SP), -4096 * 2);
			if (local_size > 3 * 4096)
				EMIT_MOV(compiler, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_SP), -4096 * 3);
		}
		else {
			EMIT_MOV(compiler, SLJIT_R0, 0, SLJIT_SP, 0);
			EMIT_MOV(compiler, SLJIT_R1, 0, SLJIT_IMM, (local_size - 1) >> 12);

			SLJIT_ASSERT (reg_map[SLJIT_R0] == 0);

			EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), -4096);
			FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
				SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 4096));
			FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
				SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 1));

			inst = (sljit_u8*)ensure_buf(compiler, 1 + 2);
			FAIL_IF(!inst);

			INC_SIZE(2);
			inst[0] = JNE_i8;
			inst[1] = (sljit_s8) -16;
		}
	}
#endif

	SLJIT_ASSERT(local_size > 0);

#if !defined(__APPLE__)
	if (options & SLJIT_F64_ALIGNMENT) {
		EMIT_MOV(compiler, SLJIT_R0, 0, SLJIT_SP, 0);

		/* Some space might allocated during sljit_grow_stack() above on WIN32. */
		FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
			SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, local_size + SSIZE_OF(sw)));

#if defined _WIN32 && !(defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
		if (compiler->local_size > 1024)
			FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
				SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, SSIZE_OF(sw)));
#endif

		inst = (sljit_u8*)ensure_buf(compiler, 1 + 6);
		FAIL_IF(!inst);

		INC_SIZE(6);
		inst[0] = GROUP_BINARY_81;
		inst[1] = MOD_REG | AND | reg_map[SLJIT_SP];
		sljit_unaligned_store_sw(inst + 2, ~(SSIZE_OF(f64) - 1));

		if (word_arg_count == 4)
			EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), compiler->locals_offset - SSIZE_OF(sw), TMP_REG1, 0);

		/* The real local size must be used. */
		return emit_mov(compiler, SLJIT_MEM1(SLJIT_SP), compiler->local_size, SLJIT_R0, 0);
	}
#endif
	FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
		SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, local_size));

	if (word_arg_count == 4)
		EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), compiler->locals_offset - SSIZE_OF(sw), TMP_REG1, 0);

	return SLJIT_SUCCESS;
}

SLJIT_API_FUNC_ATTRIBUTE sljit_s32 sljit_set_context(struct sljit_compiler *compiler,
	sljit_s32 options, sljit_s32 arg_types, sljit_s32 scratches, sljit_s32 saveds,
	sljit_s32 fscratches, sljit_s32 fsaveds, sljit_s32 local_size)
{
	sljit_s32 args_size;
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	sljit_s32 word_arg_count = 0;
#endif

	CHECK_ERROR();
	CHECK(check_sljit_set_context(compiler, options, arg_types, scratches, saveds, fscratches, fsaveds, local_size));
	set_set_context(compiler, options, arg_types, scratches, saveds, fscratches, fsaveds, local_size);

	arg_types >>= SLJIT_ARG_SHIFT;
	args_size = 0;
	while (arg_types) {
		switch (arg_types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			args_size += SSIZE_OF(f64);
			break;
		case SLJIT_ARG_TYPE_F32:
			args_size += SSIZE_OF(f32);
			break;
		default:
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
			if (word_arg_count >= 2)
				args_size += SSIZE_OF(sw);
			word_arg_count++;
#else
			args_size += SSIZE_OF(sw);
#endif
			break;
		}
		arg_types >>= SLJIT_ARG_SHIFT;
	}

	compiler->args_size = args_size;

	/* [esp+0] for saving temporaries and function calls. */
	compiler->stack_tmp_size = 2 * SSIZE_OF(sw);

#if !(defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if (scratches > 3)
		compiler->stack_tmp_size = 3 * SSIZE_OF(sw);
#endif

	compiler->saveds_offset = compiler->stack_tmp_size;
	if (scratches > 3)
		compiler->saveds_offset += ((scratches > (3 + 6)) ? 6 : (scratches - 3)) * SSIZE_OF(sw);

	compiler->locals_offset = compiler->saveds_offset;

	if (saveds > 3)
		compiler->locals_offset += (saveds - 3) * SSIZE_OF(sw);

	if (options & SLJIT_F64_ALIGNMENT)
		compiler->locals_offset = (compiler->locals_offset + SSIZE_OF(f64) - 1) & ~(SSIZE_OF(f64) - 1);

#if defined(__APPLE__)
	saveds = (2 + (scratches > 9 ? (scratches - 9) : 0) + (saveds <= 3 ? saveds : 3)) * SSIZE_OF(sw);
	compiler->local_size = ((SLJIT_LOCALS_OFFSET + saveds + local_size + 15) & ~15) - saveds;
#else
	if (options & SLJIT_F64_ALIGNMENT)
		compiler->local_size = SLJIT_LOCALS_OFFSET + ((local_size + SSIZE_OF(f64) - 1) & ~(SSIZE_OF(f64) - 1));
	else
		compiler->local_size = SLJIT_LOCALS_OFFSET + ((local_size + SSIZE_OF(sw) - 1) & ~(SSIZE_OF(sw) - 1));
#endif
	return SLJIT_SUCCESS;
}

static sljit_s32 emit_stack_frame_release(struct sljit_compiler *compiler)
{
	sljit_uw size;
	sljit_u8 *inst;

	size = (sljit_uw)(1 + (compiler->scratches > 9 ? (compiler->scratches - 9) : 0) +
		(compiler->saveds <= 3 ? compiler->saveds : 3));
	inst = (sljit_u8*)ensure_buf(compiler, 1 + size);
	FAIL_IF(!inst);

	INC_SIZE(size);

	if (compiler->saveds > 0 || compiler->scratches > 11)
		POP_REG(reg_map[SLJIT_S0]);
	if (compiler->saveds > 1 || compiler->scratches > 10)
		POP_REG(reg_map[SLJIT_S1]);
	if (compiler->saveds > 2 || compiler->scratches > 9)
		POP_REG(reg_map[SLJIT_S2]);
	POP_REG(reg_map[TMP_REG1]);

	return SLJIT_SUCCESS;
}

SLJIT_API_FUNC_ATTRIBUTE sljit_s32 sljit_emit_return_void(struct sljit_compiler *compiler)
{
	sljit_uw size;
	sljit_u8 *inst;

	CHECK_ERROR();
	CHECK(check_sljit_emit_return_void(compiler));

	SLJIT_ASSERT(compiler->args_size >= 0);
	SLJIT_ASSERT(compiler->local_size > 0);

#if !defined(__APPLE__)
	if (compiler->options & SLJIT_F64_ALIGNMENT)
		EMIT_MOV(compiler, SLJIT_SP, 0, SLJIT_MEM1(SLJIT_SP), compiler->local_size)
	else
		FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
			SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, compiler->local_size));
#else
	FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
		SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, compiler->local_size));
#endif

	FAIL_IF(emit_stack_frame_release(compiler));

	size = 1;
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if (compiler->args_size > 0)
		size = 3;
#endif
	inst = (sljit_u8*)ensure_buf(compiler, 1 + size);
	FAIL_IF(!inst);

	INC_SIZE(size);

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if (compiler->args_size > 0) {
		RET_I16(U8(compiler->args_size));
		return SLJIT_SUCCESS;
	}
#endif

	RET();
	return SLJIT_SUCCESS;
}

/* --------------------------------------------------------------------- */
/*  Call / return instructions                                           */
/* --------------------------------------------------------------------- */

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)

static sljit_sw c_fast_call_get_stack_size(sljit_s32 arg_types, sljit_s32 *word_arg_count_ptr)
{
	sljit_sw stack_size = 0;
	sljit_s32 word_arg_count = 0;

	arg_types >>= SLJIT_ARG_SHIFT;

	while (arg_types) {
		switch (arg_types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			stack_size += SSIZE_OF(f64);
			break;
		case SLJIT_ARG_TYPE_F32:
			stack_size += SSIZE_OF(f32);
			break;
		default:
			word_arg_count++;
			if (word_arg_count > 2)
				stack_size += SSIZE_OF(sw);
			break;
		}

		arg_types >>= SLJIT_ARG_SHIFT;
	}

	if (word_arg_count_ptr)
		*word_arg_count_ptr = word_arg_count;

	return stack_size;
}

static sljit_s32 c_fast_call_with_args(struct sljit_compiler *compiler,
	sljit_s32 arg_types, sljit_sw stack_size, sljit_s32 word_arg_count, sljit_s32 swap_args)
{
	sljit_u8 *inst;
	sljit_s32 float_arg_count;

	if (stack_size == SSIZE_OF(sw) && word_arg_count == 3) {
		inst = (sljit_u8*)ensure_buf(compiler, 1 + 1);
		FAIL_IF(!inst);
		INC_SIZE(1);
		PUSH_REG(reg_map[SLJIT_R2]);
	}
	else if (stack_size > 0) {
		if (word_arg_count >= 4)
			EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), compiler->saveds_offset - SSIZE_OF(sw));

		FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
			SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, stack_size));

		stack_size = 0;
		arg_types >>= SLJIT_ARG_SHIFT;
		word_arg_count = 0;
		float_arg_count = 0;
		while (arg_types) {
			switch (arg_types & SLJIT_ARG_MASK) {
			case SLJIT_ARG_TYPE_F64:
				float_arg_count++;
				FAIL_IF(emit_sse2_store(compiler, 0, SLJIT_MEM1(SLJIT_SP), stack_size, float_arg_count));
				stack_size += SSIZE_OF(f64);
				break;
			case SLJIT_ARG_TYPE_F32:
				float_arg_count++;
				FAIL_IF(emit_sse2_store(compiler, 1, SLJIT_MEM1(SLJIT_SP), stack_size, float_arg_count));
				stack_size += SSIZE_OF(f32);
				break;
			default:
				word_arg_count++;
				if (word_arg_count == 3) {
					EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), stack_size, SLJIT_R2, 0);
					stack_size += SSIZE_OF(sw);
				}
				else if (word_arg_count == 4) {
					EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), stack_size, TMP_REG1, 0);
					stack_size += SSIZE_OF(sw);
				}
				break;
			}

			arg_types >>= SLJIT_ARG_SHIFT;
		}
	}

	if (word_arg_count > 0) {
		if (swap_args) {
			inst = (sljit_u8*)ensure_buf(compiler, 1 + 1);
			FAIL_IF(!inst);
			INC_SIZE(1);

			*inst++ = U8(XCHG_EAX_r | reg_map[SLJIT_R2]);
		}
		else {
			inst = (sljit_u8*)ensure_buf(compiler, 1 + 2);
			FAIL_IF(!inst);
			INC_SIZE(2);

			*inst++ = MOV_r_rm;
			*inst++ = U8(MOD_REG | (reg_map[SLJIT_R2] << 3) | reg_map[SLJIT_R0]);
		}
	}

	return SLJIT_SUCCESS;
}

#endif

static sljit_s32 cdecl_call_get_stack_size(struct sljit_compiler *compiler, sljit_s32 arg_types, sljit_s32 *word_arg_count_ptr)
{
	sljit_sw stack_size = 0;
	sljit_s32 word_arg_count = 0;

	arg_types >>= SLJIT_ARG_SHIFT;

	while (arg_types) {
		switch (arg_types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			stack_size += SSIZE_OF(f64);
			break;
		case SLJIT_ARG_TYPE_F32:
			stack_size += SSIZE_OF(f32);
			break;
		default:
			word_arg_count++;
			stack_size += SSIZE_OF(sw);
			break;
		}

		arg_types >>= SLJIT_ARG_SHIFT;
	}

	if (word_arg_count_ptr)
		*word_arg_count_ptr = word_arg_count;

	if (stack_size <= compiler->stack_tmp_size)
		return 0;

#if defined(__APPLE__)
	return ((stack_size - compiler->stack_tmp_size + 15) & ~15);
#else
	return stack_size - compiler->stack_tmp_size;
#endif
}

static sljit_s32 cdecl_call_with_args(struct sljit_compiler *compiler,
	sljit_s32 arg_types, sljit_sw stack_size, sljit_s32 word_arg_count)
{
	sljit_s32 float_arg_count = 0;

	if (word_arg_count >= 4)
		EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), compiler->saveds_offset - SSIZE_OF(sw));

	if (stack_size > 0)
		FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
			SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, stack_size));

	stack_size = 0;
	word_arg_count = 0;
	arg_types >>= SLJIT_ARG_SHIFT;

	while (arg_types) {
		switch (arg_types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			float_arg_count++;
			FAIL_IF(emit_sse2_store(compiler, 0, SLJIT_MEM1(SLJIT_SP), stack_size, float_arg_count));
			stack_size += SSIZE_OF(f64);
			break;
		case SLJIT_ARG_TYPE_F32:
			float_arg_count++;
			FAIL_IF(emit_sse2_store(compiler, 1, SLJIT_MEM1(SLJIT_SP), stack_size, float_arg_count));
			stack_size += SSIZE_OF(f32);
			break;
		default:
			word_arg_count++;
			EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), stack_size, (word_arg_count >= 4) ? TMP_REG1 : word_arg_count, 0);
			stack_size += SSIZE_OF(sw);
			break;
		}

		arg_types >>= SLJIT_ARG_SHIFT;
	}

	return SLJIT_SUCCESS;
}

static sljit_s32 post_call_with_args(struct sljit_compiler *compiler,
	sljit_s32 arg_types, sljit_s32 stack_size)
{
	sljit_u8 *inst;
	sljit_s32 single;

	if (stack_size > 0)
		FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
			SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, stack_size));

	if ((arg_types & SLJIT_ARG_MASK) < SLJIT_ARG_TYPE_F64)
		return SLJIT_SUCCESS;

	single = ((arg_types & SLJIT_ARG_MASK) == SLJIT_ARG_TYPE_F32);

	inst = (sljit_u8*)ensure_buf(compiler, 1 + 3);
	FAIL_IF(!inst);
	INC_SIZE(3);
	inst[0] = single ? FSTPS : FSTPD;
	inst[1] = (0x03 << 3) | 0x04;
	inst[2] = (0x04 << 3) | reg_map[SLJIT_SP];

	return emit_sse2_load(compiler, single, SLJIT_FR0, SLJIT_MEM1(SLJIT_SP), 0);
}

static sljit_s32 tail_call_with_args(struct sljit_compiler *compiler,
	sljit_s32 *extra_space, sljit_s32 arg_types,
	sljit_s32 src, sljit_sw srcw)
{
	sljit_sw args_size, prev_args_size, saved_regs_size;
	sljit_sw types, word_arg_count, float_arg_count;
	sljit_sw stack_size, prev_stack_size, min_size, offset;
	sljit_sw base_reg, word_arg4_offset;
	sljit_u8 r2_offset = 0;
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	sljit_u8 fast_call = (*extra_space & 0xff) == SLJIT_CALL;
#endif
	sljit_u8* inst;

	ADJUST_LOCAL_OFFSET(src, srcw);
	CHECK_EXTRA_REGS(src, srcw, (void)0);

	saved_regs_size = (1 + (compiler->scratches > 9 ? (compiler->scratches - 9) : 0)
		+ (compiler->saveds <= 3 ? compiler->saveds : 3)) * SSIZE_OF(sw);

	word_arg_count = 0;
	float_arg_count = 0;
	arg_types >>= SLJIT_ARG_SHIFT;
	types = 0;
	args_size = 0;

	while (arg_types != 0) {
		types = (types << SLJIT_ARG_SHIFT) | (arg_types & SLJIT_ARG_MASK);

		switch (arg_types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			args_size += SSIZE_OF(f64);
			float_arg_count++;
			break;
		case SLJIT_ARG_TYPE_F32:
			args_size += SSIZE_OF(f32);
			float_arg_count++;
			break;
		default:
			word_arg_count++;
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
			if (!fast_call || word_arg_count > 2)
				args_size += SSIZE_OF(sw);
#else
			args_size += SSIZE_OF(sw);
#endif
			break;
		}
		arg_types >>= SLJIT_ARG_SHIFT;
	}

	if (args_size <= compiler->args_size) {
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
		*extra_space = fast_call ? 0 : args_size;
		prev_args_size = compiler->args_size;
		stack_size = prev_args_size + SSIZE_OF(sw) + saved_regs_size;
#else /* !SLJIT_X86_32_FASTCALL */
		*extra_space = 0;
		stack_size = args_size + SSIZE_OF(sw) + saved_regs_size;
#endif /* SLJIT_X86_32_FASTCALL */

#if !defined(__APPLE__)
		if (compiler->options & SLJIT_F64_ALIGNMENT) {
			EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), compiler->local_size);
			offset = stack_size;
			base_reg = SLJIT_MEM1(TMP_REG1);
		} else {
#endif /* !__APPLE__ */
			offset = stack_size + compiler->local_size;
			base_reg = SLJIT_MEM1(SLJIT_SP);
#if !defined(__APPLE__)
		}
#endif /* !__APPLE__ */

		if (!(src & SLJIT_IMM) && src != SLJIT_R0) {
			if (word_arg_count >= 1) {
				EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), 0, SLJIT_R0, 0);
				r2_offset = sizeof(sljit_sw);
			}
			EMIT_MOV(compiler, SLJIT_R0, 0, src, srcw);
		}

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
		if (!fast_call)
			offset -= SSIZE_OF(sw);

		if (word_arg_count >= 3) {
			word_arg4_offset = SSIZE_OF(sw);

			if (word_arg_count + float_arg_count >= 4) {
				word_arg4_offset = SSIZE_OF(sw) + SSIZE_OF(sw);
				if ((types & SLJIT_ARG_MASK) == SLJIT_ARG_TYPE_F64)
					word_arg4_offset = SSIZE_OF(sw) + SSIZE_OF(f64);
			}

			/* In cdecl mode, at least one more word value must
			 * be present on the stack before the return address. */
			EMIT_MOV(compiler, base_reg, offset - word_arg4_offset, SLJIT_R2, 0);
		}

		if (fast_call) {
			if (args_size < prev_args_size) {
				EMIT_MOV(compiler, SLJIT_R2, 0, base_reg, offset - prev_args_size - SSIZE_OF(sw));
				EMIT_MOV(compiler, base_reg, offset - args_size - SSIZE_OF(sw), SLJIT_R2, 0);
			}
		} else if (prev_args_size > 0) {
			EMIT_MOV(compiler, SLJIT_R2, 0, base_reg, offset - prev_args_size);
			EMIT_MOV(compiler, base_reg, offset, SLJIT_R2, 0);
		}
#endif /* SLJIT_X86_32_FASTCALL */

		while (types != 0) {
			switch (types & SLJIT_ARG_MASK) {
			case SLJIT_ARG_TYPE_F64:
				offset -= SSIZE_OF(f64);
				FAIL_IF(emit_sse2_store(compiler, 0, base_reg, offset, float_arg_count));
				float_arg_count--;
				break;
			case SLJIT_ARG_TYPE_F32:
				offset -= SSIZE_OF(f32);
				FAIL_IF(emit_sse2_store(compiler, 0, base_reg, offset, float_arg_count));
				float_arg_count--;
				break;
			default:
				switch (word_arg_count) {
				case 1:
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
					if (fast_call) {
						EMIT_MOV(compiler, SLJIT_R2, 0, r2_offset != 0 ? SLJIT_MEM1(SLJIT_SP) : SLJIT_R0, 0);
						break;
					}
#endif
					offset -= SSIZE_OF(sw);
					if (r2_offset != 0) {
						EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), 0);
						EMIT_MOV(compiler, base_reg, offset, SLJIT_R2, 0);
					} else
						EMIT_MOV(compiler, base_reg, offset, SLJIT_R0, 0);
					break;
				case 2:
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
					if (fast_call)
						break;
#endif
					offset -= SSIZE_OF(sw);
					EMIT_MOV(compiler, base_reg, offset, SLJIT_R1, 0);
					break;
				case 3:
					offset -= SSIZE_OF(sw);
					break;
				case 4:
					offset -= SSIZE_OF(sw);
					EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), compiler->saveds_offset - SSIZE_OF(sw));
					EMIT_MOV(compiler, base_reg, offset, SLJIT_R2, 0);
					break;
				}
				word_arg_count--;
				break;
			}
			types >>= SLJIT_ARG_SHIFT;
		}

#if !defined(__APPLE__)
		if (compiler->options & SLJIT_F64_ALIGNMENT) {
			EMIT_MOV(compiler, SLJIT_SP, 0, TMP_REG1, 0);
		} else {
#endif /* !__APPLE__ */
			FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
				SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, compiler->local_size));
#if !defined(__APPLE__)
		}
#endif /* !__APPLE__ */
		FAIL_IF(emit_stack_frame_release(compiler));

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
		if (args_size < prev_args_size)
			FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
				SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, prev_args_size - args_size));
#endif

		return SLJIT_SUCCESS;
	}

	stack_size = args_size + SSIZE_OF(sw);

	if (word_arg_count >= 1 && !(src & SLJIT_IMM) && src != SLJIT_R0) {
		r2_offset = SSIZE_OF(sw);
		stack_size += SSIZE_OF(sw);
	}

	if (word_arg_count >= 3)
		stack_size += SSIZE_OF(sw);

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	prev_args_size = compiler->args_size;
#else
	prev_args_size = 0;
#endif

	prev_stack_size = prev_args_size + SSIZE_OF(sw) + saved_regs_size;
	min_size = prev_stack_size + compiler->local_size;

	base_reg = SLJIT_MEM1(SLJIT_SP);
	word_arg4_offset = compiler->saveds_offset - SSIZE_OF(sw);

#if !defined(__APPLE__)
	if (compiler->options & SLJIT_F64_ALIGNMENT) {
		min_size += 2 * SSIZE_OF(sw);

		if (stack_size < min_size)
			stack_size = min_size;

		EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), compiler->local_size);
		FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
			TMP_REG1, 0, TMP_REG1, 0, SLJIT_IMM, stack_size - prev_stack_size));

		inst = emit_x86_instruction(compiler, 1, SLJIT_SP, 0, TMP_REG1, 0);
		FAIL_IF(!inst);
		*inst = XCHG_r_rm;

		if (src == SLJIT_MEM1(SLJIT_SP))
			src = SLJIT_MEM1(TMP_REG1);
		base_reg = SLJIT_MEM1(TMP_REG1);
	} else {
#endif /* !__APPLE__ */
		if (stack_size > min_size) {
			FAIL_IF(emit_non_cum_binary(compiler, BINARY_OPCODE(SUB),
				SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, stack_size - min_size));
			if (src == SLJIT_MEM1(SLJIT_SP))
				srcw += stack_size - min_size;
			word_arg4_offset += stack_size - min_size;
		}
		else
			stack_size = min_size;
#if !defined(__APPLE__)
	}
#endif /* !__APPLE__ */

	if (word_arg_count >= 3) {
		EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), r2_offset, SLJIT_R2, 0);

		if (word_arg_count >= 4)
			EMIT_MOV(compiler, SLJIT_R2, 0, base_reg, word_arg4_offset);
	}

	if (!(src & SLJIT_IMM) && src != SLJIT_R0) {
		if (word_arg_count >= 1) {
			SLJIT_ASSERT(r2_offset == sizeof(sljit_sw));
			EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), 0, SLJIT_R0, 0);
		}
		EMIT_MOV(compiler, SLJIT_R0, 0, src, srcw);
	}

	/* Restore saved registers. */
	offset = stack_size - prev_args_size - 2 * SSIZE_OF(sw);
	EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), offset);

	if (compiler->saveds > 2 || compiler->scratches > 9) {
		offset -= SSIZE_OF(sw);
		EMIT_MOV(compiler, SLJIT_S2, 0, SLJIT_MEM1(SLJIT_SP), offset);
	}
	if (compiler->saveds > 1 || compiler->scratches > 10) {
		offset -= SSIZE_OF(sw);
		EMIT_MOV(compiler, SLJIT_S1, 0, SLJIT_MEM1(SLJIT_SP), offset);
	}
	if (compiler->saveds > 0 || compiler->scratches > 11) {
		offset -= SSIZE_OF(sw);
		EMIT_MOV(compiler, SLJIT_S0, 0, SLJIT_MEM1(SLJIT_SP), offset);
	}

	/* Copy fourth argument and return address. */
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if (fast_call) {
		offset = stack_size;
		*extra_space = 0;

		if (word_arg_count >= 4 && prev_args_size == 0) {
			offset -= SSIZE_OF(sw);
			inst = emit_x86_instruction(compiler, 1, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), offset);
			FAIL_IF(!inst);
			*inst = XCHG_r_rm;

			SLJIT_ASSERT(args_size != prev_args_size);
		} else {
			if (word_arg_count >= 4) {
				offset -= SSIZE_OF(sw);
				EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), offset, SLJIT_R2, 0);
			}

			if (args_size != prev_args_size)
				EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), stack_size - prev_args_size - SSIZE_OF(sw));
		}

		if (args_size != prev_args_size)
			EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), stack_size - args_size - SSIZE_OF(sw), SLJIT_R2, 0);
	} else {
#endif /* SLJIT_X86_32_FASTCALL */
		offset = stack_size - SSIZE_OF(sw);
		*extra_space = args_size;

		if (word_arg_count >= 4 && prev_args_size == SSIZE_OF(sw)) {
			offset -= SSIZE_OF(sw);
			inst = emit_x86_instruction(compiler, 1, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), offset);
			FAIL_IF(!inst);
			*inst = XCHG_r_rm;

			SLJIT_ASSERT(prev_args_size > 0);
		} else {
			if (word_arg_count >= 4) {
				offset -= SSIZE_OF(sw);
				EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), offset, SLJIT_R2, 0);
			}

			if (prev_args_size > 0)
				EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), stack_size - prev_args_size - SSIZE_OF(sw));
		}

		/* Copy return address. */
		if (prev_args_size > 0)
			EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), stack_size - SSIZE_OF(sw), SLJIT_R2, 0);
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	}
#endif /* SLJIT_X86_32_FASTCALL */

	while (types != 0) {
		switch (types & SLJIT_ARG_MASK) {
		case SLJIT_ARG_TYPE_F64:
			offset -= SSIZE_OF(f64);
			FAIL_IF(emit_sse2_store(compiler, 0, SLJIT_MEM1(SLJIT_SP), offset, float_arg_count));
			float_arg_count--;
			break;
		case SLJIT_ARG_TYPE_F32:
			offset -= SSIZE_OF(f32);
			FAIL_IF(emit_sse2_store(compiler, 0, SLJIT_MEM1(SLJIT_SP), offset, float_arg_count));
			float_arg_count--;
			break;
		default:
			switch (word_arg_count) {
			case 1:
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
				if (fast_call) {
					EMIT_MOV(compiler, SLJIT_R2, 0, r2_offset != 0 ? SLJIT_MEM1(SLJIT_SP) : SLJIT_R0, 0);
					break;
				}
#endif
				offset -= SSIZE_OF(sw);
				if (r2_offset != 0) {
					EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), 0);
					EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), offset, SLJIT_R2, 0);
				} else
					EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), offset, SLJIT_R0, 0);
				break;
			case 2:
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
				if (fast_call)
					break;
#endif
				offset -= SSIZE_OF(sw);
				EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), offset, SLJIT_R1, 0);
				break;
			case 3:
				offset -= SSIZE_OF(sw);
				EMIT_MOV(compiler, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_SP), r2_offset);
				EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), offset, SLJIT_R2, 0);
				break;
			}
			word_arg_count--;
			break;
		}
		types >>= SLJIT_ARG_SHIFT;
	}

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	/* Skip return address. */
	if (fast_call)
		offset -= SSIZE_OF(sw);
#endif

	SLJIT_ASSERT(offset >= 0);

	if (offset == 0)
		return SLJIT_SUCCESS;

	return emit_cum_binary(compiler, BINARY_OPCODE(ADD),
		SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, offset);
}

static sljit_s32 emit_tail_call_end(struct sljit_compiler *compiler, sljit_s32 extra_space)
{
	/* Called when stack consumption cannot be reduced to 0. */
	sljit_u8 *inst;

	FAIL_IF(emit_cum_binary(compiler, BINARY_OPCODE(ADD),
		SLJIT_SP, 0, SLJIT_SP, 0, SLJIT_IMM, extra_space));

	inst = (sljit_u8*)ensure_buf(compiler, 1 + 1);
	FAIL_IF(!inst);
	INC_SIZE(1);
	RET();

	return SLJIT_SUCCESS;
}

SLJIT_API_FUNC_ATTRIBUTE struct sljit_jump* sljit_emit_call(struct sljit_compiler *compiler, sljit_s32 type,
	sljit_s32 arg_types)
{
	struct sljit_jump *jump;
	sljit_sw stack_size = 0;
	sljit_s32 word_arg_count;

	CHECK_ERROR_PTR();
	CHECK_PTR(check_sljit_emit_call(compiler, type, arg_types));

	if (type & SLJIT_CALL_RETURN) {
		stack_size = type;
		PTR_FAIL_IF(tail_call_with_args(compiler, &stack_size, arg_types, SLJIT_IMM, 0));

#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE) \
			|| (defined SLJIT_ARGUMENT_CHECKS && SLJIT_ARGUMENT_CHECKS)
		compiler->skip_checks = 1;
#endif

		if (stack_size == 0) {
			type = SLJIT_JUMP | (type & SLJIT_REWRITABLE_JUMP);
			return sljit_emit_jump(compiler, type);
		}

		jump = sljit_emit_jump(compiler, type);
		PTR_FAIL_IF(jump == NULL);

		PTR_FAIL_IF(emit_tail_call_end(compiler, stack_size));
		return jump;
	}

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	if ((type & 0xff) == SLJIT_CALL) {
		stack_size = c_fast_call_get_stack_size(arg_types, &word_arg_count);
		PTR_FAIL_IF(c_fast_call_with_args(compiler, arg_types, stack_size, word_arg_count, 0));

#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE) \
		|| (defined SLJIT_ARGUMENT_CHECKS && SLJIT_ARGUMENT_CHECKS)
		compiler->skip_checks = 1;
#endif

		jump = sljit_emit_jump(compiler, type);
		PTR_FAIL_IF(jump == NULL);

		PTR_FAIL_IF(post_call_with_args(compiler, arg_types, 0));
		return jump;
	}
#endif

	stack_size = cdecl_call_get_stack_size(compiler, arg_types, &word_arg_count);
	PTR_FAIL_IF(cdecl_call_with_args(compiler, arg_types, stack_size, word_arg_count));

#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE) \
		|| (defined SLJIT_ARGUMENT_CHECKS && SLJIT_ARGUMENT_CHECKS)
	compiler->skip_checks = 1;
#endif

	jump = sljit_emit_jump(compiler, type);
	PTR_FAIL_IF(jump == NULL);

	PTR_FAIL_IF(post_call_with_args(compiler, arg_types, stack_size));
	return jump;
}

SLJIT_API_FUNC_ATTRIBUTE sljit_s32 sljit_emit_icall(struct sljit_compiler *compiler, sljit_s32 type,
	sljit_s32 arg_types,
	sljit_s32 src, sljit_sw srcw)
{
	sljit_sw stack_size = 0;
	sljit_s32 word_arg_count;
#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	sljit_s32 swap_args;
#endif

	CHECK_ERROR();
	CHECK(check_sljit_emit_icall(compiler, type, arg_types, src, srcw));

	if (type & SLJIT_CALL_RETURN) {
		stack_size = type;
		FAIL_IF(tail_call_with_args(compiler, &stack_size, arg_types, src, srcw));

		if (!(src & SLJIT_IMM)) {
			src = SLJIT_R0;
			srcw = 0;
		}

#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE) \
			|| (defined SLJIT_ARGUMENT_CHECKS && SLJIT_ARGUMENT_CHECKS)
		compiler->skip_checks = 1;
#endif

		if (stack_size == 0)
			return sljit_emit_ijump(compiler, SLJIT_JUMP, src, srcw);

		FAIL_IF(sljit_emit_ijump(compiler, type, src, srcw));
		return emit_tail_call_end(compiler, stack_size);
	}

#if (defined SLJIT_X86_32_FASTCALL && SLJIT_X86_32_FASTCALL)
	SLJIT_ASSERT(reg_map[SLJIT_R0] == 0 && reg_map[SLJIT_R2] == 1 && SLJIT_R0 == 1 && SLJIT_R2 == 3);

	if ((type & 0xff) == SLJIT_CALL) {
		stack_size = c_fast_call_get_stack_size(arg_types, &word_arg_count);
		swap_args = 0;

		if (word_arg_count > 0) {
			if ((src & REG_MASK) == SLJIT_R2 || OFFS_REG(src) == SLJIT_R2) {
				swap_args = 1;
				if (((src & REG_MASK) | 0x2) == SLJIT_R2)
					src ^= 0x2;
				if ((OFFS_REG(src) | 0x2) == SLJIT_R2)
					src ^= TO_OFFS_REG(0x2);
			}
		}

		FAIL_IF(c_fast_call_with_args(compiler, arg_types, stack_size, word_arg_count, swap_args));

		compiler->saveds_offset += stack_size;
		compiler->locals_offset += stack_size;

#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE) \
		|| (defined SLJIT_ARGUMENT_CHECKS && SLJIT_ARGUMENT_CHECKS)
		compiler->skip_checks = 1;
#endif
		FAIL_IF(sljit_emit_ijump(compiler, type, src, srcw));

		compiler->saveds_offset -= stack_size;
		compiler->locals_offset -= stack_size;

		return post_call_with_args(compiler, arg_types, 0);
	}
#endif

	stack_size = cdecl_call_get_stack_size(compiler, arg_types, &word_arg_count);
	FAIL_IF(cdecl_call_with_args(compiler, arg_types, stack_size, word_arg_count));

	compiler->saveds_offset += stack_size;
	compiler->locals_offset += stack_size;

#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE) \
		|| (defined SLJIT_ARGUMENT_CHECKS && SLJIT_ARGUMENT_CHECKS)
	compiler->skip_checks = 1;
#endif
	FAIL_IF(sljit_emit_ijump(compiler, type, src, srcw));

	compiler->saveds_offset -= stack_size;
	compiler->locals_offset -= stack_size;

	return post_call_with_args(compiler, arg_types, stack_size);
}

SLJIT_API_FUNC_ATTRIBUTE sljit_s32 sljit_emit_fast_enter(struct sljit_compiler *compiler, sljit_s32 dst, sljit_sw dstw)
{
	sljit_u8 *inst;

	CHECK_ERROR();
	CHECK(check_sljit_emit_fast_enter(compiler, dst, dstw));
	ADJUST_LOCAL_OFFSET(dst, dstw);

	CHECK_EXTRA_REGS(dst, dstw, (void)0);

	if (FAST_IS_REG(dst)) {
		/* Unused dest is possible here. */
		inst = (sljit_u8*)ensure_buf(compiler, 1 + 1);
		FAIL_IF(!inst);

		INC_SIZE(1);
		POP_REG(reg_map[dst]);
		return SLJIT_SUCCESS;
	}

	/* Memory. */
	inst = emit_x86_instruction(compiler, 1, 0, 0, dst, dstw);
	FAIL_IF(!inst);
	*inst++ = POP_rm;
	return SLJIT_SUCCESS;
}

static sljit_s32 emit_fast_return(struct sljit_compiler *compiler, sljit_s32 src, sljit_sw srcw)
{
	sljit_u8 *inst;

	CHECK_EXTRA_REGS(src, srcw, (void)0);

	if (FAST_IS_REG(src)) {
		inst = (sljit_u8*)ensure_buf(compiler, 1 + 1 + 1);
		FAIL_IF(!inst);

		INC_SIZE(1 + 1);
		PUSH_REG(reg_map[src]);
	}
	else {
		inst = emit_x86_instruction(compiler, 1, 0, 0, src, srcw);
		FAIL_IF(!inst);
		*inst++ = GROUP_FF;
		*inst |= PUSH_rm;

		inst = (sljit_u8*)ensure_buf(compiler, 1 + 1);
		FAIL_IF(!inst);
		INC_SIZE(1);
	}

	RET();
	return SLJIT_SUCCESS;
}

static sljit_s32 skip_frames_before_return(struct sljit_compiler *compiler)
{
	sljit_sw size, saved_size;
	sljit_s32 has_f64_aligment;

	/* Don't adjust shadow stack if it isn't enabled.  */
	if (!cpu_has_shadow_stack ())
		return SLJIT_SUCCESS;

	SLJIT_ASSERT(compiler->args_size >= 0);
	SLJIT_ASSERT(compiler->local_size > 0);

#if !defined(__APPLE__)
	has_f64_aligment = compiler->options & SLJIT_F64_ALIGNMENT;
#else
	has_f64_aligment = 0;
#endif

	size = compiler->local_size;
	saved_size = (1 + (compiler->scratches > 9 ? (compiler->scratches - 9) : 0)
		+ (compiler->saveds <= 3 ? compiler->saveds : 3)) * SSIZE_OF(sw);

	if (has_f64_aligment) {
		/* mov TMP_REG1, [esp + local_size].  */
		EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(SLJIT_SP), size);
		/* mov TMP_REG1, [TMP_REG1+ saved_size].  */
		EMIT_MOV(compiler, TMP_REG1, 0, SLJIT_MEM1(TMP_REG1), saved_size);
		/* Move return address to [esp]. */
		EMIT_MOV(compiler, SLJIT_MEM1(SLJIT_SP), 0, TMP_REG1, 0);
		size = 0;
	} else
		size += saved_size;

	return adjust_shadow_stack(compiler, SLJIT_MEM1(SLJIT_SP), size);
}
