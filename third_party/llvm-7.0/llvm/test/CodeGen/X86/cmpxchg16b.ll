; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=x86_64-unknown- -mcpu=core2 | FileCheck %s --check-prefixes=CHECK

; Basic 128-bit cmpxchg
define void @t1(i128* nocapture %p) nounwind ssp {
; CHECK-LABEL: t1:
; CHECK:       # %bb.0: # %entry
; CHECK-NEXT:    pushq %rbx
; CHECK-NEXT:    xorl %eax, %eax
; CHECK-NEXT:    xorl %edx, %edx
; CHECK-NEXT:    xorl %ecx, %ecx
; CHECK-NEXT:    movl $1, %ebx
; CHECK-NEXT:    lock cmpxchg16b (%rdi)
; CHECK-NEXT:    popq %rbx
; CHECK-NEXT:    retq
entry:
  %r = cmpxchg i128* %p, i128 0, i128 1 seq_cst seq_cst
  ret void
}

; FIXME: Handle 128-bit atomicrmw/load atomic/store atomic
