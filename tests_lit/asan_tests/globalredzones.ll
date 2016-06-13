; Test of global redzone layout

; REQUIRES: allow_dump

; RUN: %p2i -i %s --args -threads=0 -fsanitize-address \
; RUN:     | %iflc FileCheck %s
; RUN: %p2i -i %s --args -verbose=global_init,inst -threads=0 \
; RUN:     -fsanitize-address | %iflc FileCheck --check-prefix=DUMP %s

; The array of redzones

; DUMP-LABEL: ========= Instrumented Globals =========
; DUMP: @__$rz0 = internal constant <{ i32, i32, i32, i32, i32, i32 }>
; DUMP:         <{ i32 ptrtoint ([32 x i8]* @__$rz2 to i32), i32 ptrtoint ([32 x i8]* @__$rz3 to i32),
; DUMP:         i32 ptrtoint ([32 x i8]* @__$rz4 to i32), i32 ptrtoint ([32 x i8]* @__$rz5 to i32),
; DUMP:         i32 ptrtoint ([32 x i8]* @__$rz6 to i32), i32 ptrtoint ([32 x i8]* @__$rz7 to i32) }>
; DUMP-NEXT: @__$rz1 = internal constant [4 x i8] c"\06\00\00\00"                                                                                                 

; CHECK-LABEL: .type   __$rz0,%object
; CHECK-NEXT: .section   .rodata
; CHECK-NEXT: __$rz0:
; CHECK-NEXT: .long   __$rz2
; CHECK-NEXT: .long   __$rz3
; CHECK-NEXT: .long   __$rz4
; CHECK-NEXT: .long   __$rz5
; CHECK-NEXT: .long   __$rz6
; CHECK-NEXT: .long   __$rz7
; CHECK-LABEL: .type   __$rz1,%object
; CHECK-NEXT: .section   .rodata
; CHECK-NEXT: __$rz1:
; CHECK-NEXT: .byte   6
; CHECK-NEXT: .byte   0
; CHECK-NEXT: .byte   0
; CHECK-NEXT: .byte   0

; A zero-initialized global
@zeroInitGlobal = internal global [32 x i8] zeroinitializer

; DUMP-NEXT: @__$rz2 = internal global [32 x i8] zeroinitializer
; DUMP-NEXT: @zeroInitGlobal = internal global [32 x i8] zeroinitializer
; DUMP-NEXT: @__$rz3 = internal global [32 x i8] zeroinitializer

; CHECK-LABEL: .type   __$rz2,%object
; CHECK-NEXT: .section   .bss
; CHECK-NEXT: __$rz2:
; CHECK-LABEL: .type   zeroInitGlobal,%object
; CHECK-NEXT: .section   .bss
; CHECK-NEXT: zeroInitGlobal:
; CHECK-LABEL: .type   __$rz3,%object
; CHECK-NEXT: .section   .bss
; CHECK-NEXT: __$rz3:

; A constant-initialized global
@constInitGlobal = internal constant [32 x i8] c"ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"

; CHECK-LABEL: .type   __$rz4,%object
; CHECK-NEXT: .section   .rodata
; CHECK-NEXT: __$rz4:
; CHECK-LABEL: .type   constInitGlobal,%object
; CHECK-NEXT: .section   .rodata
; CHECK-NEXT: constInitGlobal:
; CHECK-LABEL: .type   __$rz5,%object
; CHECK-NEXT: .section   .rodata
; CHECK-NEXT: __$rz5:

; DUMP-NEXT: @__$rz4 = internal constant [32 x i8] c"RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR"
; DUMP-NEXT: @constInitGlobal = internal constant [32 x i8] c"ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"
; DUMP-NEXT: @__$rz5 = internal constant [32 x i8] c"RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR"

; A regular global
@regInitGlobal = internal global [32 x i8] c"ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"

; DUMP-NEXT: @__$rz6 = internal global [32 x i8] c"RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR"
; DUMP-NEXT: @regInitGlobal = internal global [32 x i8] c"ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"
; DUMP-NEXT: @__$rz7 = internal global [32 x i8] c"RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR"

; CHECK-LABEL: .type   __$rz6,%object
; CHECK-NEXT: .section   .data
; CHECK-NEXT: __$rz6:
; CHECK-LABEL: .type   regInitGlobal,%object
; CHECK-NEXT: .section   .data
; CHECK-NEXT: regInitGlobal:
; CHECK-LABEL: .type   __$rz7,%object
; CHECK-NEXT: .section   .data
; CHECK-NEXT: __$rz7:

define internal void @func() {
  ret void
}

; DUMP-LABEL: define internal void @func() {
; CHECK-LABEL: func: