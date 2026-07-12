// REQUIRES: bedrock-registered-target
// RUN: %clang_cc1 -triple bedrock -std=c11 -ffreestanding -O1 -target-feature +virtaccel -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s | FileCheck %s

#include <bedrocksystemintrin.h>

// CHECK-LABEL: define{{.*}} i64 @sysreg
// CHECK: call void @llvm.bedrock.write.status(i64
// CHECK: call i64 @llvm.bedrock.read.control.register(i32 3)
// CHECK: call void @llvm.bedrock.write.control.register(i32 4, i64
// CHECK: call i64 @llvm.bedrock.read.segment.register(i32 1)
// CHECK: call void @llvm.bedrock.write.segment.register(i32 7, i64
uint64_t sysreg(uint64_t value) {
  __bedrock_write_status(value);
  uint64_t cr = __bedrock_read_control_register(3);
  __bedrock_write_control_register(4, value);
  uint64_t segment = __bedrock_read_segment_register(__BEDROCK_SEG_DS);
  __bedrock_write_segment_register(__BEDROCK_SEG_GS4, value);
  return cr + segment;
}

// CHECK-LABEL: define{{.*}} void @cache
// CHECK: br i1
// CHECK: call void @llvm.bedrock.flush.dcache(ptr %{{.*}}, i64 1)
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

// CHECK-LABEL: define{{.*}} void @state
// CHECK: call void @llvm.bedrock.save.processor.state(ptr
// CHECK: call void @llvm.bedrock.restore.processor.state(ptr
void state(void *area) {
  __bedrock_save_processor_state(area);
  __bedrock_restore_processor_state(area);
}

// CHECK-LABEL: define{{.*}} i64 @encode
// CHECK: call i64 @llvm.bedrock.encode.instruction(ptr
intptr_t encode(void *destination,
                const __bedrock_instruction_descriptor_t *descriptor) {
  return __bedrock_encode_instruction(destination, descriptor);
}

