// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -O1 -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s | FileCheck %s

#include <bedrocksystemintrin.h>

// CHECK: @packed_segment_image ={{.*}} global i64 305418371, align 8
// CHECK: @based_segment_image ={{.*}} global i64 305418502, align 8
// CHECK: @disabled_segment_image ={{.*}} global i64 0, align 8
uint64_t packed_segment_image =
    __BEDROCK_SEGMENT_IMAGE(0x12345, 33, 65, 2);
uint64_t based_segment_image =
    __BEDROCK_SEGMENT_IMAGE_FOR_BASE(0x12345fff, 2, 3, 0);
uint64_t disabled_segment_image = __BEDROCK_SEGMENT_DISABLED;

_Static_assert(__BEDROCK_SEGMENT_IMAGE(0x12345, 33, 65, 2) ==
                   UINT64_C(0x12345083),
               "segment fields must be masked and packed");
_Static_assert(__BEDROCK_SEGMENT_IMAGE_FOR_BASE(0x12345fff, 2, 3, 0) ==
                   UINT64_C(0x12345106),
               "byte bases must be converted to page bases");
_Static_assert(__BEDROCK_SEGMENT_DISABLED == 0,
               "the disabled image must be canonical");

// CHECK-LABEL: define{{.*}} i64 @segment_image_single_evaluation
// CHECK-COUNT-4: store i64
// CHECK-NOT: store i64
// CHECK: ret i64
uint64_t segment_image_single_evaluation(uint64_t *values) {
  return __BEDROCK_SEGMENT_IMAGE(values[0]++, values[1]++, values[2]++,
                                 values[3]++);
}

// CHECK-LABEL: define{{.*}} i64 @sysreg
// CHECK: call void @llvm.bedrock.write.status(i64
// CHECK: call i64 @llvm.bedrock.read.control.register(i32 4097)
// CHECK: call void @llvm.bedrock.write.control.register(i32 4352, i64
// CHECK: call i64 @llvm.bedrock.read.segment.register(i32 0)
// CHECK: call i64 @llvm.bedrock.read.code.segment()
// CHECK: call void @llvm.bedrock.write.segment.register(i32 6, i64
uint64_t sysreg(uint64_t value) {
  __bedrock_write_status(value);
  uint64_t cr = __bedrock_read_control_register(__BEDROCK_CR_BOOTCFG);
  __bedrock_write_control_register(__BEDROCK_CR_PMC, value);
  uint64_t segment = __bedrock_read_segment_register(__BEDROCK_SEG_DS);
  uint64_t code_segment = __bedrock_read_code_segment();
  __bedrock_write_segment_register(__BEDROCK_SEG_GS4, value);
  return cr + segment + code_segment;
}

// CHECK-LABEL: define{{.*}} void @cache
// CHECK: br i1
// CHECK: call i64 @llvm.bedrock.cpuid(i64 8590000129)
// CHECK: and i64 %{{.*}}, 65535
// CHECK: sub{{( nsw)?}} i64 0, %{{.*}}
// CHECK: and i64 %{{.*}}, %{{.*}}
// CHECK: call void @llvm.bedrock.flush.dcache(ptr %{{.*}}, i64 1)
// CHECK: add i64 %{{.*}}, %{{.*}}
// CHECK: call void @llvm.bedrock.invalidate.dcache(ptr %{{.*}}, i64 1)
// CHECK: call void @llvm.bedrock.invalidate.icache(ptr %{{.*}}, i64 1)
// CHECK: call void @llvm.bedrock.writeback.dcache(ptr %{{.*}}, i64 1)
// CHECK: call void @llvm.bedrock.sync.cache(ptr %{{.*}}, i64 1)
void cache(void *pointer, size_t length) {
  __bedrock_flush_dcache(pointer, length);
  __bedrock_invalidate_dcache(pointer, length);
  __bedrock_invalidate_icache(pointer, length);
  __bedrock_writeback_dcache(pointer, length);
  __bedrock_sync_cache(pointer, length);
}

// CHECK-LABEL: define{{.*}} void @zero_length
// CHECK-NOT: llvm.bedrock.{{.*}}cache
// CHECK: ret void
void zero_length(void *pointer) { __bedrock_sync_cache(pointer, 0); }

// CHECK-LABEL: define{{.*}} {{.*}} @mmu
// CHECK: call void @llvm.bedrock.invalidate.tlb()
// CHECK: call void @llvm.bedrock.invalidate.page(ptr
// CHECK: call void @llvm.bedrock.invalidate.asid(i32 5)
// CHECK: call void @llvm.bedrock.switch.page.table(i64
// CHECK: call void @llvm.bedrock.switch.page.table.asid(i64 {{.*}}, i64 6)
// CHECK: call { i64, i64 } @llvm.bedrock.virtual.to.physical(i64
// CHECK: call { i64, i64 } @llvm.bedrock.page.table.query(i32 2, i64
__bedrock_query_result_t mmu(uint64_t address) {
  __bedrock_invalidate_tlb();
  __bedrock_invalidate_page((void *)address);
  __bedrock_invalidate_asid(5);
  __bedrock_switch_page_table(address);
  __bedrock_switch_page_table_asid(address, 6);
  __bedrock_query_result_t first = __bedrock_virtual_to_physical(address);
  __bedrock_query_result_t second = __bedrock_page_table_query(2, address);
  first.value += second.value;
  first.flags |= second.flags;
  return first;
}

extern uint64_t *get_query_value_output(void);
extern uint16_t *get_query_flags_output(void);

// CHECK-LABEL: define{{.*}} void @ordered_query_arguments
// CHECK: [[VTOP_VALUE:%.*]] = {{.*}}call ptr @get_query_value_output()
// CHECK: [[VTOP_FLAGS:%.*]] = {{.*}}call ptr @get_query_flags_output()
// CHECK: call { i64, i64 } @llvm.bedrock.virtual.to.physical(i64
// CHECK: store i64 {{.*}}, ptr [[VTOP_VALUE]]
// CHECK: store i16 {{.*}}, ptr [[VTOP_FLAGS]]
// CHECK: [[PTQ_VALUE:%.*]] = {{.*}}call ptr @get_query_value_output()
// CHECK: [[PTQ_FLAGS:%.*]] = {{.*}}call ptr @get_query_flags_output()
// CHECK: call { i64, i64 } @llvm.bedrock.page.table.query(i32 2, i64
// CHECK: store i64 {{.*}}, ptr [[PTQ_VALUE]]
// CHECK: store i16 {{.*}}, ptr [[PTQ_FLAGS]]
void ordered_query_arguments(uint64_t address) {
  __builtin_bedrock_virtual_to_physical(
      address, get_query_value_output(), get_query_flags_output());
  __builtin_bedrock_page_table_query(
      2, address, get_query_value_output(), get_query_flags_output());
}

// CHECK-LABEL: define{{.*}} void @state
// CHECK: call void @llvm.bedrock.save.processor.state(ptr
// CHECK: call void @llvm.bedrock.restore.processor.state(ptr
void state(void *area) {
  __bedrock_save_processor_state(area);
  __bedrock_restore_processor_state(area);
}
