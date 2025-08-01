/*
 * Copyright (c) 1999, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "classfile/vmClasses.hpp"
#include "compiler/compilationMemoryStatistic.hpp"
#include "compiler/compilerDefinitions.inline.hpp"
#include "jfr/support/jfrIntrinsics.hpp"
#include "opto/c2compiler.hpp"
#include "opto/compile.hpp"
#include "opto/optoreg.hpp"
#include "opto/output.hpp"
#include "opto/runtime.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/macros.hpp"


// register information defined by ADLC
extern const char register_save_policy[];
extern const int  register_save_type[];

const char* C2Compiler::retry_no_subsuming_loads() {
  return "retry without subsuming loads";
}
const char* C2Compiler::retry_no_escape_analysis() {
  return "retry without escape analysis";
}
const char* C2Compiler::retry_no_locks_coarsening() {
  return "retry without locks coarsening";
}
const char* C2Compiler::retry_no_iterative_escape_analysis() {
  return "retry without iterative escape analysis";
}
const char* C2Compiler::retry_no_reduce_allocation_merges() {
  return "retry without reducing allocation merges";
}
const char* C2Compiler::retry_no_superword() {
  return "retry without SuperWord";
}

void compiler_stubs_init(bool in_compiler_thread);

bool C2Compiler::init_c2_runtime() {

#ifdef ASSERT
  if (!AlignVector && VerifyAlignVector) {
    warning("VerifyAlignVector disabled because AlignVector is not enabled.");
    FLAG_SET_CMDLINE(VerifyAlignVector, false);
  }
#endif

  // Check assumptions used while running ADLC
  Compile::adlc_verification();
  assert(REG_COUNT <= ConcreteRegisterImpl::number_of_registers, "incompatible register counts");

  for (int i = 0; i < ConcreteRegisterImpl::number_of_registers ; i++ ) {
      OptoReg::vm2opto[i] = OptoReg::Bad;
  }

  for( OptoReg::Name i=OptoReg::Name(0); i<OptoReg::Name(REG_COUNT); i = OptoReg::add(i,1) ) {
    VMReg r = OptoReg::as_VMReg(i);
    if (r->is_valid()) {
      OptoReg::vm2opto[r->value()] = i;
    }
  }

  DEBUG_ONLY( Node::init_NodeProperty(); )

  compiler_stubs_init(true /* in_compiler_thread */); // generate compiler's intrinsics stubs

  Compile::pd_compiler2_init();

  CompilerThread* thread = CompilerThread::current();

  HandleMark handle_mark(thread);
  return OptoRuntime::generate(thread->env());
}

void C2Compiler::initialize() {
  assert(!CompilerConfig::is_c1_or_interpreter_only_no_jvmci(), "C2 compiler is launched, it's not c1/interpreter only mode");
  // The first compiler thread that gets here will initialize the
  // small amount of global state (and runtime stubs) that C2 needs.

  // There is a race possible once at startup and then we're fine

  // Note that this is being called from a compiler thread not the
  // main startup thread.
  if (should_perform_init()) {
    bool successful = C2Compiler::init_c2_runtime();
    int new_state = (successful) ? initialized : failed;
    set_state(new_state);
  }
}

void C2Compiler::compile_method(ciEnv* env, ciMethod* target, int entry_bci, bool install_code, DirectiveSet* directive) {
  assert(is_initialized(), "Compiler thread must be initialized");

  CompilationMemoryStatisticMark cmsm(directive);

  bool subsume_loads = SubsumeLoads;
  bool do_escape_analysis = DoEscapeAnalysis;
  bool do_iterative_escape_analysis = DoEscapeAnalysis;
  bool do_reduce_allocation_merges = ReduceAllocationMerges && EliminateAllocations;
  bool eliminate_boxing = EliminateAutoBox;
  bool do_locks_coarsening = EliminateLocks;
  bool do_superword = UseSuperWord;

  while (!env->failing()) {
    ResourceMark rm;
    // Attempt to compile while subsuming loads into machine instructions.
    Options options(subsume_loads,
                    do_escape_analysis,
                    do_iterative_escape_analysis,
                    do_reduce_allocation_merges,
                    eliminate_boxing,
                    do_locks_coarsening,
                    do_superword,
                    install_code);
    Compile C(env, target, entry_bci, options, directive);

    // Check result and retry if appropriate.
    if (C.failure_reason() != nullptr) {
      if (C.failure_reason_is(retry_no_subsuming_loads())) {
        assert(subsume_loads, "must make progress");
        subsume_loads = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_escape_analysis())) {
        assert(do_escape_analysis, "must make progress");
        do_escape_analysis = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_iterative_escape_analysis())) {
        assert(do_iterative_escape_analysis, "must make progress");
        do_iterative_escape_analysis = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_reduce_allocation_merges())) {
        assert(do_reduce_allocation_merges, "must make progress");
        do_reduce_allocation_merges = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_locks_coarsening())) {
        assert(do_locks_coarsening, "must make progress");
        do_locks_coarsening = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.failure_reason_is(retry_no_superword())) {
        assert(do_superword, "must make progress");
        do_superword = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      if (C.has_boxed_value()) {
        // Recompile without boxing elimination regardless failure reason.
        assert(eliminate_boxing, "must make progress");
        eliminate_boxing = false;
        env->report_failure(C.failure_reason());
        continue;  // retry
      }
      // Pass any other failure reason up to the ciEnv.
      // Note that serious, irreversible failures are already logged
      // on the ciEnv via env->record_method_not_compilable().
      env->record_failure(C.failure_reason());
    }
    if (StressRecompilation) {
      if (subsume_loads) {
        subsume_loads = false;
        continue;  // retry
      }
      if (do_escape_analysis) {
        do_escape_analysis = false;
        continue;  // retry
      }
      if (do_locks_coarsening) {
        do_locks_coarsening = false;
        continue;  // retry
      }
    }
    // print inlining for last compilation only
    C.dump_print_inlining();

    // No retry; just break the loop.
    break;
  }
}

void C2Compiler::print_timers() {
  Compile::print_timers();
}

bool C2Compiler::is_intrinsic_supported(const methodHandle& method) {
  vmIntrinsics::ID id = method->intrinsic_id();
  return C2Compiler::is_intrinsic_supported(id);
}

bool C2Compiler::is_intrinsic_supported(vmIntrinsics::ID id) {
  assert(id != vmIntrinsics::_none, "must be a VM intrinsic");

  if (id < vmIntrinsics::FIRST_ID || id > vmIntrinsics::LAST_COMPILER_INLINE) {
    return false;
  }

  switch (id) {
  case vmIntrinsics::_compressStringC:
  case vmIntrinsics::_compressStringB:
    if (!Matcher::match_rule_supported(Op_StrCompressedCopy)) return false;
    break;
  case vmIntrinsics::_inflateStringC:
  case vmIntrinsics::_inflateStringB:
    if (!Matcher::match_rule_supported(Op_StrInflatedCopy)) return false;
    break;
  case vmIntrinsics::_compareToL:
  case vmIntrinsics::_compareToU:
  case vmIntrinsics::_compareToLU:
  case vmIntrinsics::_compareToUL:
    if (!Matcher::match_rule_supported(Op_StrComp)) return false;
    break;
  case vmIntrinsics::_equalsL:
    if (!Matcher::match_rule_supported(Op_StrEquals)) return false;
    break;
  case vmIntrinsics::_vectorizedHashCode:
    if (!Matcher::match_rule_supported(Op_VectorizedHashCode)) return false;
    break;
  case vmIntrinsics::_equalsB:
  case vmIntrinsics::_equalsC:
    if (!Matcher::match_rule_supported(Op_AryEq)) return false;
    break;
  case vmIntrinsics::_copyMemory:
    if (StubRoutines::unsafe_arraycopy() == nullptr) return false;
    break;
  case vmIntrinsics::_setMemory:
    if (StubRoutines::unsafe_setmemory() == nullptr) return false;
    break;
  case vmIntrinsics::_electronicCodeBook_encryptAESCrypt:
    if (StubRoutines::electronicCodeBook_encryptAESCrypt() == nullptr) return false;
    break;
  case vmIntrinsics::_electronicCodeBook_decryptAESCrypt:
    if (StubRoutines::electronicCodeBook_decryptAESCrypt() == nullptr) return false;
    break;
  case vmIntrinsics::_galoisCounterMode_AESCrypt:
    if (StubRoutines::galoisCounterMode_AESCrypt() == nullptr) return false;
    break;
  case vmIntrinsics::_bigIntegerRightShiftWorker:
    if (StubRoutines::bigIntegerRightShift() == nullptr) return false;
    break;
  case vmIntrinsics::_bigIntegerLeftShiftWorker:
    if (StubRoutines::bigIntegerLeftShift() == nullptr) return false;
    break;
  case vmIntrinsics::_encodeAsciiArray:
    if (!Matcher::match_rule_supported(Op_EncodeISOArray) || !Matcher::supports_encode_ascii_array) return false;
    break;
  case vmIntrinsics::_encodeISOArray:
  case vmIntrinsics::_encodeByteISOArray:
    if (!Matcher::match_rule_supported(Op_EncodeISOArray)) return false;
    break;
  case vmIntrinsics::_countPositives:
    if (!Matcher::match_rule_supported(Op_CountPositives))  return false;
    break;
  case vmIntrinsics::_bitCount_i:
    if (!Matcher::match_rule_supported(Op_PopCountI)) return false;
    break;
  case vmIntrinsics::_bitCount_l:
    if (!Matcher::match_rule_supported(Op_PopCountL)) return false;
    break;
  case vmIntrinsics::_compress_i:
  case vmIntrinsics::_compress_l:
    if (!Matcher::match_rule_supported(Op_CompressBits)) return false;
    break;
  case vmIntrinsics::_expand_i:
  case vmIntrinsics::_expand_l:
    if (!Matcher::match_rule_supported(Op_ExpandBits)) return false;
    break;
  case vmIntrinsics::_numberOfLeadingZeros_i:
    if (!Matcher::match_rule_supported(Op_CountLeadingZerosI)) return false;
    break;
  case vmIntrinsics::_numberOfLeadingZeros_l:
    if (!Matcher::match_rule_supported(Op_CountLeadingZerosL)) return false;
    break;
  case vmIntrinsics::_numberOfTrailingZeros_i:
    if (!Matcher::match_rule_supported(Op_CountTrailingZerosI)) return false;
    break;
  case vmIntrinsics::_numberOfTrailingZeros_l:
    if (!Matcher::match_rule_supported(Op_CountTrailingZerosL)) return false;
    break;
  case vmIntrinsics::_reverse_i:
    if (!Matcher::match_rule_supported(Op_ReverseI)) return false;
    break;
  case vmIntrinsics::_reverse_l:
    if (!Matcher::match_rule_supported(Op_ReverseL)) return false;
    break;
  case vmIntrinsics::_reverseBytes_c:
    if (!Matcher::match_rule_supported(Op_ReverseBytesUS)) return false;
    break;
  case vmIntrinsics::_reverseBytes_s:
    if (!Matcher::match_rule_supported(Op_ReverseBytesS)) return false;
    break;
  case vmIntrinsics::_reverseBytes_i:
    if (!Matcher::match_rule_supported(Op_ReverseBytesI)) return false;
    break;
  case vmIntrinsics::_reverseBytes_l:
    if (!Matcher::match_rule_supported(Op_ReverseBytesL)) return false;
    break;
  case vmIntrinsics::_compareUnsigned_i:
    if (!Matcher::match_rule_supported(Op_CmpU3)) return false;
    break;
  case vmIntrinsics::_compareUnsigned_l:
    if (!Matcher::match_rule_supported(Op_CmpUL3)) return false;
    break;
  case vmIntrinsics::_divideUnsigned_i:
    if (!Matcher::match_rule_supported(Op_UDivI)) return false;
    break;
  case vmIntrinsics::_remainderUnsigned_i:
    if (!Matcher::match_rule_supported(Op_UModI)) return false;
    break;
  case vmIntrinsics::_divideUnsigned_l:
    if (!Matcher::match_rule_supported(Op_UDivL)) return false;
    break;
  case vmIntrinsics::_remainderUnsigned_l:
    if (!Matcher::match_rule_supported(Op_UModL)) return false;
    break;
  case vmIntrinsics::_float16ToFloat:
    if (!Matcher::match_rule_supported(Op_ConvHF2F)) return false;
    break;
  case vmIntrinsics::_floatToFloat16:
    if (!Matcher::match_rule_supported(Op_ConvF2HF)) return false;
    break;
  case vmIntrinsics::_sqrt_float16:
    if (!Matcher::match_rule_supported(Op_SqrtHF)) return false;
    break;
  case vmIntrinsics::_fma_float16:
    if (!Matcher::match_rule_supported(Op_FmaHF)) return false;
    break;

  /* CompareAndSet, Object: */
  case vmIntrinsics::_compareAndSetReference:
#ifdef _LP64
    if ( UseCompressedOops && !Matcher::match_rule_supported(Op_CompareAndSwapN)) return false;
    if (!UseCompressedOops && !Matcher::match_rule_supported(Op_CompareAndSwapP)) return false;
#else
    if (!Matcher::match_rule_supported(Op_CompareAndSwapP)) return false;
#endif
    break;
  case vmIntrinsics::_weakCompareAndSetReferencePlain:
  case vmIntrinsics::_weakCompareAndSetReferenceAcquire:
  case vmIntrinsics::_weakCompareAndSetReferenceRelease:
  case vmIntrinsics::_weakCompareAndSetReference:
#ifdef _LP64
    if ( UseCompressedOops && !Matcher::match_rule_supported(Op_WeakCompareAndSwapN)) return false;
    if (!UseCompressedOops && !Matcher::match_rule_supported(Op_WeakCompareAndSwapP)) return false;
#else
    if (!Matcher::match_rule_supported(Op_WeakCompareAndSwapP)) return false;
#endif
    break;
  /* CompareAndSet, Long: */
  case vmIntrinsics::_compareAndSetLong:
    if (!Matcher::match_rule_supported(Op_CompareAndSwapL)) return false;
    break;
  case vmIntrinsics::_weakCompareAndSetLongPlain:
  case vmIntrinsics::_weakCompareAndSetLongAcquire:
  case vmIntrinsics::_weakCompareAndSetLongRelease:
  case vmIntrinsics::_weakCompareAndSetLong:
    if (!Matcher::match_rule_supported(Op_WeakCompareAndSwapL)) return false;
    break;

  /* CompareAndSet, Int: */
  case vmIntrinsics::_compareAndSetInt:
    if (!Matcher::match_rule_supported(Op_CompareAndSwapI)) return false;
    break;
  case vmIntrinsics::_weakCompareAndSetIntPlain:
  case vmIntrinsics::_weakCompareAndSetIntAcquire:
  case vmIntrinsics::_weakCompareAndSetIntRelease:
  case vmIntrinsics::_weakCompareAndSetInt:
    if (!Matcher::match_rule_supported(Op_WeakCompareAndSwapI)) return false;
    break;

  /* CompareAndSet, Byte: */
  case vmIntrinsics::_compareAndSetByte:
    if (!Matcher::match_rule_supported(Op_CompareAndSwapB)) return false;
    break;
  case vmIntrinsics::_weakCompareAndSetBytePlain:
  case vmIntrinsics::_weakCompareAndSetByteAcquire:
  case vmIntrinsics::_weakCompareAndSetByteRelease:
  case vmIntrinsics::_weakCompareAndSetByte:
    if (!Matcher::match_rule_supported(Op_WeakCompareAndSwapB)) return false;
    break;

  /* CompareAndSet, Short: */
  case vmIntrinsics::_compareAndSetShort:
    if (!Matcher::match_rule_supported(Op_CompareAndSwapS)) return false;
    break;
  case vmIntrinsics::_weakCompareAndSetShortPlain:
  case vmIntrinsics::_weakCompareAndSetShortAcquire:
  case vmIntrinsics::_weakCompareAndSetShortRelease:
  case vmIntrinsics::_weakCompareAndSetShort:
    if (!Matcher::match_rule_supported(Op_WeakCompareAndSwapS)) return false;
    break;

  /* CompareAndExchange, Object: */
  case vmIntrinsics::_compareAndExchangeReference:
  case vmIntrinsics::_compareAndExchangeReferenceAcquire:
  case vmIntrinsics::_compareAndExchangeReferenceRelease:
#ifdef _LP64
    if ( UseCompressedOops && !Matcher::match_rule_supported(Op_CompareAndExchangeN)) return false;
    if (!UseCompressedOops && !Matcher::match_rule_supported(Op_CompareAndExchangeP)) return false;
#else
    if (!Matcher::match_rule_supported(Op_CompareAndExchangeP)) return false;
#endif
    break;

  /* CompareAndExchange, Long: */
  case vmIntrinsics::_compareAndExchangeLong:
  case vmIntrinsics::_compareAndExchangeLongAcquire:
  case vmIntrinsics::_compareAndExchangeLongRelease:
    if (!Matcher::match_rule_supported(Op_CompareAndExchangeL)) return false;
    break;

  /* CompareAndExchange, Int: */
  case vmIntrinsics::_compareAndExchangeInt:
  case vmIntrinsics::_compareAndExchangeIntAcquire:
  case vmIntrinsics::_compareAndExchangeIntRelease:
    if (!Matcher::match_rule_supported(Op_CompareAndExchangeI)) return false;
    break;

  /* CompareAndExchange, Byte: */
  case vmIntrinsics::_compareAndExchangeByte:
  case vmIntrinsics::_compareAndExchangeByteAcquire:
  case vmIntrinsics::_compareAndExchangeByteRelease:
    if (!Matcher::match_rule_supported(Op_CompareAndExchangeB)) return false;
    break;

  /* CompareAndExchange, Short: */
  case vmIntrinsics::_compareAndExchangeShort:
  case vmIntrinsics::_compareAndExchangeShortAcquire:
  case vmIntrinsics::_compareAndExchangeShortRelease:
    if (!Matcher::match_rule_supported(Op_CompareAndExchangeS)) return false;
    break;

  case vmIntrinsics::_getAndAddByte:
    if (!Matcher::match_rule_supported(Op_GetAndAddB)) return false;
    break;
  case vmIntrinsics::_getAndAddShort:
    if (!Matcher::match_rule_supported(Op_GetAndAddS)) return false;
    break;
  case vmIntrinsics::_getAndAddInt:
    if (!Matcher::match_rule_supported(Op_GetAndAddI)) return false;
    break;
  case vmIntrinsics::_getAndAddLong:
    if (!Matcher::match_rule_supported(Op_GetAndAddL)) return false;
    break;

  case vmIntrinsics::_getAndSetByte:
    if (!Matcher::match_rule_supported(Op_GetAndSetB)) return false;
    break;
  case vmIntrinsics::_getAndSetShort:
    if (!Matcher::match_rule_supported(Op_GetAndSetS)) return false;
    break;
  case vmIntrinsics::_getAndSetInt:
    if (!Matcher::match_rule_supported(Op_GetAndSetI)) return false;
    break;
  case vmIntrinsics::_getAndSetLong:
    if (!Matcher::match_rule_supported(Op_GetAndSetL)) return false;
    break;
  case vmIntrinsics::_getAndSetReference:
#ifdef _LP64
    if (!UseCompressedOops && !Matcher::match_rule_supported(Op_GetAndSetP)) return false;
    if (UseCompressedOops && !Matcher::match_rule_supported(Op_GetAndSetN)) return false;
    break;
#else
    if (!Matcher::match_rule_supported(Op_GetAndSetP)) return false;
    break;
#endif
  case vmIntrinsics::_incrementExactI:
  case vmIntrinsics::_addExactI:
    if (!Matcher::match_rule_supported(Op_OverflowAddI)) return false;
    break;
  case vmIntrinsics::_incrementExactL:
  case vmIntrinsics::_addExactL:
    if (!Matcher::match_rule_supported(Op_OverflowAddL)) return false;
    break;
  case vmIntrinsics::_decrementExactI:
  case vmIntrinsics::_subtractExactI:
    if (!Matcher::match_rule_supported(Op_OverflowSubI)) return false;
    break;
  case vmIntrinsics::_decrementExactL:
  case vmIntrinsics::_subtractExactL:
    if (!Matcher::match_rule_supported(Op_OverflowSubL)) return false;
    break;
  case vmIntrinsics::_negateExactI:
    if (!Matcher::match_rule_supported(Op_OverflowSubI)) return false;
    break;
  case vmIntrinsics::_negateExactL:
    if (!Matcher::match_rule_supported(Op_OverflowSubL)) return false;
    break;
  case vmIntrinsics::_multiplyExactI:
    if (!Matcher::match_rule_supported(Op_OverflowMulI)) return false;
    break;
  case vmIntrinsics::_multiplyExactL:
    if (!Matcher::match_rule_supported(Op_OverflowMulL)) return false;
    break;
  case vmIntrinsics::_multiplyHigh:
    if (!Matcher::match_rule_supported(Op_MulHiL)) return false;
    break;
  case vmIntrinsics::_unsignedMultiplyHigh:
    if (!Matcher::match_rule_supported(Op_UMulHiL)) return false;
    break;
  case vmIntrinsics::_getCallerClass:
    if (vmClasses::reflect_CallerSensitive_klass() == nullptr) return false;
    break;
  case vmIntrinsics::_onSpinWait:
    if (!Matcher::match_rule_supported(Op_OnSpinWait)) return false;
    break;
  case vmIntrinsics::_fmaD:
    if (!Matcher::match_rule_supported(Op_FmaD)) return false;
    break;
  case vmIntrinsics::_fmaF:
    if (!Matcher::match_rule_supported(Op_FmaF)) return false;
    break;
  case vmIntrinsics::_isDigit:
    if (!Matcher::match_rule_supported(Op_Digit)) return false;
    break;
  case vmIntrinsics::_isLowerCase:
    if (!Matcher::match_rule_supported(Op_LowerCase)) return false;
    break;
  case vmIntrinsics::_isUpperCase:
    if (!Matcher::match_rule_supported(Op_UpperCase)) return false;
    break;
  case vmIntrinsics::_isWhitespace:
    if (!Matcher::match_rule_supported(Op_Whitespace)) return false;
    break;
  case vmIntrinsics::_maxF:
  case vmIntrinsics::_maxF_strict:
    if (!Matcher::match_rule_supported(Op_MaxF)) return false;
    break;
  case vmIntrinsics::_minF:
  case vmIntrinsics::_minF_strict:
    if (!Matcher::match_rule_supported(Op_MinF)) return false;
    break;
  case vmIntrinsics::_maxD:
  case vmIntrinsics::_maxD_strict:
    if (!Matcher::match_rule_supported(Op_MaxD)) return false;
    break;
  case vmIntrinsics::_minD:
  case vmIntrinsics::_minD_strict:
    if (!Matcher::match_rule_supported(Op_MinD)) return false;
    break;
  case vmIntrinsics::_writeback0:
    if (!Matcher::match_rule_supported(Op_CacheWB)) return false;
    break;
  case vmIntrinsics::_writebackPreSync0:
    if (!Matcher::match_rule_supported(Op_CacheWBPreSync)) return false;
    break;
  case vmIntrinsics::_writebackPostSync0:
    if (!Matcher::match_rule_supported(Op_CacheWBPostSync)) return false;
    break;
  case vmIntrinsics::_rint:
  case vmIntrinsics::_ceil:
  case vmIntrinsics::_floor:
    if (!Matcher::match_rule_supported(Op_RoundDoubleMode)) return false;
    break;
  case vmIntrinsics::_dcopySign:
    if (!Matcher::match_rule_supported(Op_CopySignD)) return false;
    break;
  case vmIntrinsics::_fcopySign:
    if (!Matcher::match_rule_supported(Op_CopySignF)) return false;
    break;
  case vmIntrinsics::_dsignum:
    if (!Matcher::match_rule_supported(Op_SignumD)) return false;
    break;
  case vmIntrinsics::_fsignum:
    if (!Matcher::match_rule_supported(Op_SignumF)) return false;
    break;
  case vmIntrinsics::_floatIsInfinite:
    if (!Matcher::match_rule_supported(Op_IsInfiniteF)) return false;
    break;
  case vmIntrinsics::_floatIsFinite:
    if (!Matcher::match_rule_supported(Op_IsFiniteF)) return false;
    break;
  case vmIntrinsics::_doubleIsInfinite:
    if (!Matcher::match_rule_supported(Op_IsInfiniteD)) return false;
    break;
  case vmIntrinsics::_doubleIsFinite:
    if (!Matcher::match_rule_supported(Op_IsFiniteD)) return false;
    break;
  case vmIntrinsics::_hashCode:
  case vmIntrinsics::_identityHashCode:
  case vmIntrinsics::_getClass:
  case vmIntrinsics::_dsin:
  case vmIntrinsics::_dcos:
  case vmIntrinsics::_dtan:
  case vmIntrinsics::_dtanh:
  case vmIntrinsics::_dcbrt:
  case vmIntrinsics::_dabs:
  case vmIntrinsics::_fabs:
  case vmIntrinsics::_iabs:
  case vmIntrinsics::_labs:
  case vmIntrinsics::_datan2:
  case vmIntrinsics::_dsqrt:
  case vmIntrinsics::_dsqrt_strict:
  case vmIntrinsics::_dexp:
  case vmIntrinsics::_dlog:
  case vmIntrinsics::_dlog10:
  case vmIntrinsics::_dpow:
  case vmIntrinsics::_roundD:
  case vmIntrinsics::_roundF:
  case vmIntrinsics::_min:
  case vmIntrinsics::_max:
  case vmIntrinsics::_min_strict:
  case vmIntrinsics::_max_strict:
  case vmIntrinsics::_maxL:
  case vmIntrinsics::_minL:
  case vmIntrinsics::_arraycopy:
  case vmIntrinsics::_arraySort:
  case vmIntrinsics::_arrayPartition:
  case vmIntrinsics::_indexOfL:
  case vmIntrinsics::_indexOfU:
  case vmIntrinsics::_indexOfUL:
  case vmIntrinsics::_indexOfIL:
  case vmIntrinsics::_indexOfIU:
  case vmIntrinsics::_indexOfIUL:
  case vmIntrinsics::_indexOfU_char:
  case vmIntrinsics::_indexOfL_char:
  case vmIntrinsics::_toBytesStringU:
  case vmIntrinsics::_getCharsStringU:
  case vmIntrinsics::_getCharStringU:
  case vmIntrinsics::_putCharStringU:
  case vmIntrinsics::_getReference:
  case vmIntrinsics::_getBoolean:
  case vmIntrinsics::_getByte:
  case vmIntrinsics::_getShort:
  case vmIntrinsics::_getChar:
  case vmIntrinsics::_getInt:
  case vmIntrinsics::_getLong:
  case vmIntrinsics::_getFloat:
  case vmIntrinsics::_getDouble:
  case vmIntrinsics::_putReference:
  case vmIntrinsics::_putBoolean:
  case vmIntrinsics::_putByte:
  case vmIntrinsics::_putShort:
  case vmIntrinsics::_putChar:
  case vmIntrinsics::_putInt:
  case vmIntrinsics::_putLong:
  case vmIntrinsics::_putFloat:
  case vmIntrinsics::_putDouble:
  case vmIntrinsics::_getReferenceVolatile:
  case vmIntrinsics::_getBooleanVolatile:
  case vmIntrinsics::_getByteVolatile:
  case vmIntrinsics::_getShortVolatile:
  case vmIntrinsics::_getCharVolatile:
  case vmIntrinsics::_getIntVolatile:
  case vmIntrinsics::_getLongVolatile:
  case vmIntrinsics::_getFloatVolatile:
  case vmIntrinsics::_getDoubleVolatile:
  case vmIntrinsics::_putReferenceVolatile:
  case vmIntrinsics::_putBooleanVolatile:
  case vmIntrinsics::_putByteVolatile:
  case vmIntrinsics::_putShortVolatile:
  case vmIntrinsics::_putCharVolatile:
  case vmIntrinsics::_putIntVolatile:
  case vmIntrinsics::_putLongVolatile:
  case vmIntrinsics::_putFloatVolatile:
  case vmIntrinsics::_putDoubleVolatile:
  case vmIntrinsics::_getReferenceAcquire:
  case vmIntrinsics::_getBooleanAcquire:
  case vmIntrinsics::_getByteAcquire:
  case vmIntrinsics::_getShortAcquire:
  case vmIntrinsics::_getCharAcquire:
  case vmIntrinsics::_getIntAcquire:
  case vmIntrinsics::_getLongAcquire:
  case vmIntrinsics::_getFloatAcquire:
  case vmIntrinsics::_getDoubleAcquire:
  case vmIntrinsics::_putReferenceRelease:
  case vmIntrinsics::_putBooleanRelease:
  case vmIntrinsics::_putByteRelease:
  case vmIntrinsics::_putShortRelease:
  case vmIntrinsics::_putCharRelease:
  case vmIntrinsics::_putIntRelease:
  case vmIntrinsics::_putLongRelease:
  case vmIntrinsics::_putFloatRelease:
  case vmIntrinsics::_putDoubleRelease:
  case vmIntrinsics::_getReferenceOpaque:
  case vmIntrinsics::_getBooleanOpaque:
  case vmIntrinsics::_getByteOpaque:
  case vmIntrinsics::_getShortOpaque:
  case vmIntrinsics::_getCharOpaque:
  case vmIntrinsics::_getIntOpaque:
  case vmIntrinsics::_getLongOpaque:
  case vmIntrinsics::_getFloatOpaque:
  case vmIntrinsics::_getDoubleOpaque:
  case vmIntrinsics::_putReferenceOpaque:
  case vmIntrinsics::_putBooleanOpaque:
  case vmIntrinsics::_putByteOpaque:
  case vmIntrinsics::_putShortOpaque:
  case vmIntrinsics::_putCharOpaque:
  case vmIntrinsics::_putIntOpaque:
  case vmIntrinsics::_putLongOpaque:
  case vmIntrinsics::_putFloatOpaque:
  case vmIntrinsics::_putDoubleOpaque:
  case vmIntrinsics::_getShortUnaligned:
  case vmIntrinsics::_getCharUnaligned:
  case vmIntrinsics::_getIntUnaligned:
  case vmIntrinsics::_getLongUnaligned:
  case vmIntrinsics::_putShortUnaligned:
  case vmIntrinsics::_putCharUnaligned:
  case vmIntrinsics::_putIntUnaligned:
  case vmIntrinsics::_putLongUnaligned:
  case vmIntrinsics::_loadFence:
  case vmIntrinsics::_storeFence:
  case vmIntrinsics::_storeStoreFence:
  case vmIntrinsics::_fullFence:
  case vmIntrinsics::_currentCarrierThread:
  case vmIntrinsics::_currentThread:
  case vmIntrinsics::_setCurrentThread:
  case vmIntrinsics::_scopedValueCache:
  case vmIntrinsics::_setScopedValueCache:
  case vmIntrinsics::_Continuation_pin:
  case vmIntrinsics::_Continuation_unpin:
#ifdef JFR_HAVE_INTRINSICS
  case vmIntrinsics::_counterTime:
  case vmIntrinsics::_getEventWriter:
  case vmIntrinsics::_jvm_commit:
#endif
  case vmIntrinsics::_currentTimeMillis:
  case vmIntrinsics::_nanoTime:
  case vmIntrinsics::_allocateInstance:
  case vmIntrinsics::_allocateUninitializedArray:
  case vmIntrinsics::_newArray:
  case vmIntrinsics::_getLength:
  case vmIntrinsics::_copyOf:
  case vmIntrinsics::_copyOfRange:
  case vmIntrinsics::_clone:
  case vmIntrinsics::_isAssignableFrom:
  case vmIntrinsics::_isInstance:
  case vmIntrinsics::_isHidden:
  case vmIntrinsics::_getSuperclass:
  case vmIntrinsics::_getClassAccessFlags:
  case vmIntrinsics::_floatToRawIntBits:
  case vmIntrinsics::_floatToIntBits:
  case vmIntrinsics::_intBitsToFloat:
  case vmIntrinsics::_doubleToRawLongBits:
  case vmIntrinsics::_doubleToLongBits:
  case vmIntrinsics::_longBitsToDouble:
  case vmIntrinsics::_Reference_get0:
  case vmIntrinsics::_Reference_refersTo0:
  case vmIntrinsics::_PhantomReference_refersTo0:
  case vmIntrinsics::_Reference_clear0:
  case vmIntrinsics::_PhantomReference_clear0:
  case vmIntrinsics::_Class_cast:
  case vmIntrinsics::_aescrypt_encryptBlock:
  case vmIntrinsics::_aescrypt_decryptBlock:
  case vmIntrinsics::_cipherBlockChaining_encryptAESCrypt:
  case vmIntrinsics::_cipherBlockChaining_decryptAESCrypt:
  case vmIntrinsics::_counterMode_AESCrypt:
  case vmIntrinsics::_md5_implCompress:
  case vmIntrinsics::_sha_implCompress:
  case vmIntrinsics::_sha2_implCompress:
  case vmIntrinsics::_sha5_implCompress:
  case vmIntrinsics::_sha3_implCompress:
  case vmIntrinsics::_double_keccak:
  case vmIntrinsics::_digestBase_implCompressMB:
  case vmIntrinsics::_multiplyToLen:
  case vmIntrinsics::_squareToLen:
  case vmIntrinsics::_mulAdd:
  case vmIntrinsics::_montgomeryMultiply:
  case vmIntrinsics::_montgomerySquare:
  case vmIntrinsics::_vectorizedMismatch:
  case vmIntrinsics::_ghash_processBlocks:
  case vmIntrinsics::_chacha20Block:
  case vmIntrinsics::_kyberNtt:
  case vmIntrinsics::_kyberInverseNtt:
  case vmIntrinsics::_kyberNttMult:
  case vmIntrinsics::_kyberAddPoly_2:
  case vmIntrinsics::_kyberAddPoly_3:
  case vmIntrinsics::_kyber12To16:
  case vmIntrinsics::_kyberBarrettReduce:
  case vmIntrinsics::_dilithiumAlmostNtt:
  case vmIntrinsics::_dilithiumAlmostInverseNtt:
  case vmIntrinsics::_dilithiumNttMult:
  case vmIntrinsics::_dilithiumMontMulByConstant:
  case vmIntrinsics::_dilithiumDecomposePoly:
  case vmIntrinsics::_base64_encodeBlock:
  case vmIntrinsics::_base64_decodeBlock:
  case vmIntrinsics::_poly1305_processBlocks:
  case vmIntrinsics::_intpoly_montgomeryMult_P256:
  case vmIntrinsics::_intpoly_assign:
  case vmIntrinsics::_updateCRC32:
  case vmIntrinsics::_updateBytesCRC32:
  case vmIntrinsics::_updateByteBufferCRC32:
  case vmIntrinsics::_updateBytesCRC32C:
  case vmIntrinsics::_updateDirectByteBufferCRC32C:
  case vmIntrinsics::_updateBytesAdler32:
  case vmIntrinsics::_updateByteBufferAdler32:
  case vmIntrinsics::_profileBoolean:
  case vmIntrinsics::_isCompileConstant:
  case vmIntrinsics::_Preconditions_checkIndex:
  case vmIntrinsics::_Preconditions_checkLongIndex:
  case vmIntrinsics::_getObjectSize:
    break;
  case vmIntrinsics::_VectorCompressExpand:
  case vmIntrinsics::_VectorUnaryOp:
  case vmIntrinsics::_VectorBinaryOp:
  case vmIntrinsics::_VectorTernaryOp:
  case vmIntrinsics::_VectorFromBitsCoerced:
  case vmIntrinsics::_VectorLoadOp:
  case vmIntrinsics::_VectorLoadMaskedOp:
  case vmIntrinsics::_VectorStoreOp:
  case vmIntrinsics::_VectorStoreMaskedOp:
  case vmIntrinsics::_VectorSelectFromTwoVectorOp:
  case vmIntrinsics::_VectorGatherOp:
  case vmIntrinsics::_VectorScatterOp:
  case vmIntrinsics::_VectorReductionCoerced:
  case vmIntrinsics::_VectorTest:
  case vmIntrinsics::_VectorBlend:
  case vmIntrinsics::_VectorRearrange:
  case vmIntrinsics::_VectorSelectFrom:
  case vmIntrinsics::_VectorCompare:
  case vmIntrinsics::_VectorBroadcastInt:
  case vmIntrinsics::_VectorConvert:
  case vmIntrinsics::_VectorInsert:
  case vmIntrinsics::_VectorExtract:
  case vmIntrinsics::_VectorMaskOp:
  case vmIntrinsics::_IndexVector:
  case vmIntrinsics::_IndexPartiallyInUpperRange:
    return EnableVectorSupport;
  case vmIntrinsics::_VectorUnaryLibOp:
  case vmIntrinsics::_VectorBinaryLibOp:
    return EnableVectorSupport && Matcher::supports_vector_calling_convention();
  case vmIntrinsics::_blackhole:
#if INCLUDE_JVMTI
  case vmIntrinsics::_notifyJvmtiVThreadStart:
  case vmIntrinsics::_notifyJvmtiVThreadEnd:
  case vmIntrinsics::_notifyJvmtiVThreadMount:
  case vmIntrinsics::_notifyJvmtiVThreadUnmount:
  case vmIntrinsics::_notifyJvmtiVThreadDisableSuspend:
#endif
    break;

  default:
    return false;
  }
  return true;
}

int C2Compiler::initial_code_buffer_size(int const_size) {
  // See Compile::init_scratch_buffer_blob
  int locs_size = sizeof(relocInfo) * PhaseOutput::MAX_locs_size;
  int slop = 2 * CodeSection::end_slop(); // space between sections
  return PhaseOutput::MAX_inst_size + PhaseOutput::MAX_stubs_size + const_size + slop + locs_size;
}
