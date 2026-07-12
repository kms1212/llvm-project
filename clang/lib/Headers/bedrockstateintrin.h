#ifndef __BEDROCKSTATEINTRIN_H
#define __BEDROCKSTATEINTRIN_H

static __inline__ void __bedrock_save_processor_state(void *save_area) {
  __builtin_bedrock_save_processor_state(save_area);
}
static __inline__ void
__bedrock_restore_processor_state(const void *save_area) {
  __builtin_bedrock_restore_processor_state(save_area);
}

#endif
