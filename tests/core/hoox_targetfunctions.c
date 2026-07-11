#include "hxglib.h"

#ifdef _MSC_VER
# define HOOX_NOINLINE __declspec (noinline)
#else
# define HOOX_NOINLINE __attribute__ ((noinline))
#endif

hx_pointer HOOX_NOINLINE
hoox_test_target_function (HxString * str)
{
  if (str != NULL)
    hx_string_append_c (str, '|');
  else
    hx_usleep (HX_USEC_PER_SEC / 100);

  return NULL;
}

static hx_uint hoox_test_target_functions_counter = 0;

hx_pointer HOOX_NOINLINE
hoox_test_target_nop_function_a (hx_pointer data)
{
  hoox_test_target_functions_counter++;

  return HX_SIZE_TO_POINTER (0x1337);
}

hx_pointer HOOX_NOINLINE
hoox_test_target_nop_function_b (hx_pointer data)
{
  hoox_test_target_functions_counter += 2;

  return HX_SIZE_TO_POINTER (2);
}

hx_pointer HOOX_NOINLINE
hoox_test_target_nop_function_c (hx_pointer data)
{
  hoox_test_target_functions_counter += 3;

  hoox_test_target_nop_function_a (data);

  return HX_SIZE_TO_POINTER (3);
}
