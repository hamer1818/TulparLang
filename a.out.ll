; ModuleID = 'tulpar_aot_module'
source_filename = "tulpar_aot_module"

%struct.VMValue = type { i32, i64 }

@fmt_str = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg = private unnamed_addr constant [23 x i8] c"=== Try-Catch Test ===\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.2 = private unnamed_addr constant [37 x i8] c"\0A-- Test 1: Basic throw and catch --\00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.4 = private unnamed_addr constant [17 x i8] c"Inside try block\00", align 1
@str_lit = private unnamed_addr constant [18 x i8] c"This is an error!\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.6 = private unnamed_addr constant [22 x i8] c"This should NOT print\00", align 1
@str_lit.7 = private unnamed_addr constant [19 x i8] c"Caught exception: \00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.9 = private unnamed_addr constant [16 x i8] c"After try-catch\00", align 1
@fmt_str.10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.11 = private unnamed_addr constant [28 x i8] c"\0A-- Test 2: Object throw --\00", align 1
@key_str = private unnamed_addr constant [8 x i8] c"message\00", align 1
@str_lit.12 = private unnamed_addr constant [21 x i8] c"Something went wrong\00", align 1
@key_str.13 = private unnamed_addr constant [5 x i8] c"code\00", align 1
@fmt_str.14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.15 = private unnamed_addr constant [21 x i8] c"Caught error object:\00", align 1
@str_lit.16 = private unnamed_addr constant [10 x i8] c"Message: \00", align 1
@str_lit.17 = private unnamed_addr constant [8 x i8] c"message\00", align 1
@fmt_str.18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.19 = private unnamed_addr constant [29 x i8] c"\0A-- Test 3: Finally block --\00", align 1
@fmt_str.20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.21 = private unnamed_addr constant [20 x i8] c"Try block executing\00", align 1
@str_lit.22 = private unnamed_addr constant [19 x i8] c"Error with finally\00", align 1
@str_lit.23 = private unnamed_addr constant [9 x i8] c"Caught: \00", align 1
@fmt_str.24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.25 = private unnamed_addr constant [32 x i8] c"Finally block - always executes\00", align 1
@fmt_str.26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.27 = private unnamed_addr constant [28 x i8] c"\0A-- Test 4: No exception --\00", align 1
@fmt_str.28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.29 = private unnamed_addr constant [21 x i8] c"Try block - no error\00", align 1
@str_lit.30 = private unnamed_addr constant [9 x i8] c"Result: \00", align 1
@fmt_str.31 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.32 = private unnamed_addr constant [22 x i8] c"This should NOT print\00", align 1
@fmt_str.33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.34 = private unnamed_addr constant [20 x i8] c"Finally runs anyway\00", align 1
@fmt_str.35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@str_arg.36 = private unnamed_addr constant [30 x i8] c"\0A=== All tests completed! ===\00", align 1

declare i32 @printf(ptr, ...)

declare ptr @vm_alloc_string_aot(ptr, ptr, i32)

declare void @print_value(%struct.VMValue)

declare void @vm_binary_op(ptr, ptr, ptr, i32, ptr)

declare ptr @vm_allocate_array_aot_wrapper(ptr)

declare void @vm_array_push_aot_wrapper(ptr, ptr, %struct.VMValue)

declare %struct.VMValue @vm_array_get(ptr, i32)

declare void @vm_array_set(ptr, i32, %struct.VMValue)

declare ptr @vm_allocate_object_aot_wrapper(ptr)

declare void @vm_object_set_aot_wrapper(ptr, ptr, ptr, %struct.VMValue)

declare %struct.VMValue @vm_get_element(%struct.VMValue, %struct.VMValue)

declare void @vm_set_element(ptr, %struct.VMValue, %struct.VMValue, %struct.VMValue)

declare %struct.VMValue @aot_to_string(%struct.VMValue)

declare i64 @aot_to_int(%struct.VMValue)

declare %struct.VMValue @aot_to_json(%struct.VMValue)

declare double @aot_to_float(%struct.VMValue)

declare i64 @aot_len(%struct.VMValue)

declare void @aot_array_push(%struct.VMValue, %struct.VMValue)

declare %struct.VMValue @aot_array_pop(%struct.VMValue)

declare %struct.VMValue @aot_input()

declare %struct.VMValue @aot_trim(%struct.VMValue)

declare %struct.VMValue @aot_replace(%struct.VMValue, %struct.VMValue, %struct.VMValue)

declare %struct.VMValue @aot_split(%struct.VMValue, %struct.VMValue)

declare %struct.VMValue @aot_read_file(%struct.VMValue)

declare %struct.VMValue @aot_write_file(%struct.VMValue, %struct.VMValue)

declare %struct.VMValue @aot_append_file(%struct.VMValue, %struct.VMValue)

declare %struct.VMValue @aot_file_exists(%struct.VMValue)

declare ptr @aot_try_push()

declare void @aot_try_pop()

declare void @aot_throw(%struct.VMValue)

declare %struct.VMValue @aot_get_exception()

declare i32 @setjmp(ptr)

define i32 @main() {
entry:
  %0 = call i32 (ptr, ...) @printf(ptr @fmt_str, ptr @str_arg)
  %1 = call i32 (ptr, ...) @printf(ptr @fmt_str.1, ptr @str_arg.2)
  %eh_buf = call ptr @aot_try_push()
  %setjmp_res = call i32 @setjmp(ptr %eh_buf)
  %is_try = icmp eq i32 %setjmp_res, 0
  br i1 %is_try, label %try, label %catch

try:                                              ; preds = %entry
  %2 = call i32 (ptr, ...) @printf(ptr @fmt_str.3, ptr @str_arg.4)
  %alloc_str = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit, i32 17)
  %ptr_int = ptrtoint ptr %alloc_str to i64
  %3 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int, 1
  call void @aot_throw(%struct.VMValue %3)
  unreachable
  %4 = call i32 (ptr, ...) @printf(ptr @fmt_str.5, ptr @str_arg.6)
  call void @aot_try_pop()
  br label %try_end

catch:                                            ; preds = %entry
  %exception = call %struct.VMValue @aot_get_exception()
  %e = alloca %struct.VMValue, align 8
  store %struct.VMValue %exception, ptr %e, align 4
  %alloc_str1 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.7, i32 18)
  %ptr_int2 = ptrtoint ptr %alloc_str1 to i64
  %5 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int2, 1
  %e3 = load %struct.VMValue, ptr %e, align 4
  %L_ptr = alloca %struct.VMValue, align 8
  store %struct.VMValue %5, ptr %L_ptr, align 4
  %R_ptr = alloca %struct.VMValue, align 8
  store %struct.VMValue %e3, ptr %R_ptr, align 4
  %res_ptr = alloca %struct.VMValue, align 8
  call void @vm_binary_op(ptr null, ptr %L_ptr, ptr %R_ptr, i32 31, ptr %res_ptr)
  %bin_op_res = load %struct.VMValue, ptr %res_ptr, align 4
  call void @print_value(%struct.VMValue %bin_op_res)
  br label %try_end

try_end:                                          ; preds = %catch, %try
  %6 = call i32 (ptr, ...) @printf(ptr @fmt_str.8, ptr @str_arg.9)
  %7 = call i32 (ptr, ...) @printf(ptr @fmt_str.10, ptr @str_arg.11)
  %eh_buf4 = call ptr @aot_try_push()
  %setjmp_res5 = call i32 @setjmp(ptr %eh_buf4)
  %is_try6 = icmp eq i32 %setjmp_res5, 0
  br i1 %is_try6, label %try7, label %catch8

try7:                                             ; preds = %try_end
  %alloc_obj = call ptr @vm_allocate_object_aot_wrapper(ptr null)
  %alloc_str10 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.12, i32 20)
  %ptr_int11 = ptrtoint ptr %alloc_str10 to i64
  %8 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int11, 1
  call void @vm_object_set_aot_wrapper(ptr null, ptr %alloc_obj, ptr @key_str, %struct.VMValue %8)
  call void @vm_object_set_aot_wrapper(ptr null, ptr %alloc_obj, ptr @key_str.13, %struct.VMValue { i32 0, i64 500 })
  %ptr_int12 = ptrtoint ptr %alloc_obj to i64
  %9 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int12, 1
  %error = alloca %struct.VMValue, align 8
  store %struct.VMValue %9, ptr %error, align 4
  %error13 = load %struct.VMValue, ptr %error, align 4
  call void @aot_throw(%struct.VMValue %error13)
  unreachable
  call void @aot_try_pop()
  br label %try_end9

catch8:                                           ; preds = %try_end
  %exception14 = call %struct.VMValue @aot_get_exception()
  %err = alloca %struct.VMValue, align 8
  store %struct.VMValue %exception14, ptr %err, align 4
  %10 = call i32 (ptr, ...) @printf(ptr @fmt_str.14, ptr @str_arg.15)
  %alloc_str15 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.16, i32 9)
  %ptr_int16 = ptrtoint ptr %alloc_str15 to i64
  %11 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int16, 1
  %err17 = load %struct.VMValue, ptr %err, align 4
  %alloc_str18 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.17, i32 7)
  %ptr_int19 = ptrtoint ptr %alloc_str18 to i64
  %12 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int19, 1
  %element_val = call %struct.VMValue @vm_get_element(%struct.VMValue %err17, %struct.VMValue %12)
  %L_ptr20 = alloca %struct.VMValue, align 8
  store %struct.VMValue %11, ptr %L_ptr20, align 4
  %R_ptr21 = alloca %struct.VMValue, align 8
  store %struct.VMValue %element_val, ptr %R_ptr21, align 4
  %res_ptr22 = alloca %struct.VMValue, align 8
  call void @vm_binary_op(ptr null, ptr %L_ptr20, ptr %R_ptr21, i32 31, ptr %res_ptr22)
  %bin_op_res23 = load %struct.VMValue, ptr %res_ptr22, align 4
  call void @print_value(%struct.VMValue %bin_op_res23)
  br label %try_end9

try_end9:                                         ; preds = %catch8, %try7
  %13 = call i32 (ptr, ...) @printf(ptr @fmt_str.18, ptr @str_arg.19)
  %eh_buf24 = call ptr @aot_try_push()
  %setjmp_res25 = call i32 @setjmp(ptr %eh_buf24)
  %is_try26 = icmp eq i32 %setjmp_res25, 0
  br i1 %is_try26, label %try27, label %catch28

try27:                                            ; preds = %try_end9
  %14 = call i32 (ptr, ...) @printf(ptr @fmt_str.20, ptr @str_arg.21)
  %alloc_str30 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.22, i32 18)
  %ptr_int31 = ptrtoint ptr %alloc_str30 to i64
  %15 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int31, 1
  call void @aot_throw(%struct.VMValue %15)
  unreachable
  call void @aot_try_pop()
  br label %finally

catch28:                                          ; preds = %try_end9
  %exception32 = call %struct.VMValue @aot_get_exception()
  %e33 = alloca %struct.VMValue, align 8
  store %struct.VMValue %exception32, ptr %e33, align 4
  %alloc_str34 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.23, i32 8)
  %ptr_int35 = ptrtoint ptr %alloc_str34 to i64
  %16 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int35, 1
  %e36 = load %struct.VMValue, ptr %e, align 4
  %L_ptr37 = alloca %struct.VMValue, align 8
  store %struct.VMValue %16, ptr %L_ptr37, align 4
  %R_ptr38 = alloca %struct.VMValue, align 8
  store %struct.VMValue %e36, ptr %R_ptr38, align 4
  %res_ptr39 = alloca %struct.VMValue, align 8
  call void @vm_binary_op(ptr null, ptr %L_ptr37, ptr %R_ptr38, i32 31, ptr %res_ptr39)
  %bin_op_res40 = load %struct.VMValue, ptr %res_ptr39, align 4
  call void @print_value(%struct.VMValue %bin_op_res40)
  br label %finally

finally:                                          ; preds = %catch28, %try27
  %17 = call i32 (ptr, ...) @printf(ptr @fmt_str.24, ptr @str_arg.25)
  br label %try_end29

try_end29:                                        ; preds = %finally
  %18 = call i32 (ptr, ...) @printf(ptr @fmt_str.26, ptr @str_arg.27)
  %eh_buf41 = call ptr @aot_try_push()
  %setjmp_res42 = call i32 @setjmp(ptr %eh_buf41)
  %is_try43 = icmp eq i32 %setjmp_res42, 0
  br i1 %is_try43, label %try44, label %catch45

try44:                                            ; preds = %try_end29
  %19 = call i32 (ptr, ...) @printf(ptr @fmt_str.28, ptr @str_arg.29)
  %L_ptr48 = alloca %struct.VMValue, align 8
  store %struct.VMValue { i32 0, i64 5 }, ptr %L_ptr48, align 4
  %R_ptr49 = alloca %struct.VMValue, align 8
  store %struct.VMValue { i32 0, i64 3 }, ptr %R_ptr49, align 4
  %res_ptr50 = alloca %struct.VMValue, align 8
  call void @vm_binary_op(ptr null, ptr %L_ptr48, ptr %R_ptr49, i32 31, ptr %res_ptr50)
  %bin_op_res51 = load %struct.VMValue, ptr %res_ptr50, align 4
  %x = alloca %struct.VMValue, align 8
  store %struct.VMValue %bin_op_res51, ptr %x, align 4
  %alloc_str52 = call ptr @vm_alloc_string_aot(ptr null, ptr @str_lit.30, i32 8)
  %ptr_int53 = ptrtoint ptr %alloc_str52 to i64
  %20 = insertvalue %struct.VMValue { i32 4, i64 undef }, i64 %ptr_int53, 1
  %x54 = load %struct.VMValue, ptr %x, align 4
  %to_str = call %struct.VMValue @aot_to_string(%struct.VMValue %x54)
  %L_ptr55 = alloca %struct.VMValue, align 8
  store %struct.VMValue %20, ptr %L_ptr55, align 4
  %R_ptr56 = alloca %struct.VMValue, align 8
  store %struct.VMValue %to_str, ptr %R_ptr56, align 4
  %res_ptr57 = alloca %struct.VMValue, align 8
  call void @vm_binary_op(ptr null, ptr %L_ptr55, ptr %R_ptr56, i32 31, ptr %res_ptr57)
  %bin_op_res58 = load %struct.VMValue, ptr %res_ptr57, align 4
  call void @print_value(%struct.VMValue %bin_op_res58)
  call void @aot_try_pop()
  br label %finally46

catch45:                                          ; preds = %try_end29
  %exception59 = call %struct.VMValue @aot_get_exception()
  %e60 = alloca %struct.VMValue, align 8
  store %struct.VMValue %exception59, ptr %e60, align 4
  %21 = call i32 (ptr, ...) @printf(ptr @fmt_str.31, ptr @str_arg.32)
  br label %finally46

finally46:                                        ; preds = %catch45, %try44
  %22 = call i32 (ptr, ...) @printf(ptr @fmt_str.33, ptr @str_arg.34)
  br label %try_end47

try_end47:                                        ; preds = %finally46
  %23 = call i32 (ptr, ...) @printf(ptr @fmt_str.35, ptr @str_arg.36)
  ret i32 0
}
