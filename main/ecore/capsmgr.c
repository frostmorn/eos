#include "capsmgr.h"
#include "error.h"
#include "emisc/fancymacro.h"
#include "emisc/kvec.h"
#include <soc/soc_caps.h>
#include <esp_bit_defs.h>

extern bool esp_gpio_is_reserved(uint64_t bitmask);

static kvec_t(eos_cap_t) ecaps;


bool eos_cap_claim(eos_cap_type_t type, int32_t no, eos_dev_t *dev){
  // Invalid vap type
  if (type < 0 || type >= EOS_CAPS_COUNT)
  {
    EOS_LOGE("Wrong cap used\n");
    abort();
  }

  // Invalid cap index
  if (no < 0) return false;

  // Seek if cap already claimed
  for (size_t i = 0; i < kv_size(ecaps); i++){
    if ((kv_A(ecaps, i).type == type) && (kv_A(ecaps, i).no == no))
      return false;
  }

  // Claiming cap
  eos_cap_t newcap  = {.type = type, .no = no, .dev = dev};
  kv_push(eos_cap_t, ecaps, newcap);

  EOS_LOGI("Claimed cap %d %d for dev %p", type, no, dev);
  
  return true;
}

bool eos_cap_release(eos_cap_type_t type, int32_t no, eos_dev_t *dev){
  for (size_t i = 0; i < kv_size(ecaps); i++){
    if ((kv_A(ecaps, i).type == type) && 
       (kv_A(ecaps, i).no == no) && 
       (kv_A(ecaps, i).dev == dev)
    ){
      kv_drop_fast(eos_cap_t, ecaps, i);
      return true;
    }
  }
  return false;
}
// soc/gpio_sig_map.h per soc
void eos_capsmgr_init(){
  kv_init(ecaps);
  // Claim reserved gpios 
  
  for (int32_t i = 0; i < SOC_GPIO_PIN_COUNT; i++){
    if (esp_gpio_is_reserved(BIT64(i)))
      eos_cap_claim(EOS_CAPS_GPIO, i, NULL);
  }
  
}
