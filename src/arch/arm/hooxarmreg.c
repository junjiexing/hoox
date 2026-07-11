/*
 * Copyright (C) 2015-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxarmreg.h"

void
hoox_arm_reg_describe (hx_arm_reg reg,
                      HooxArmRegInfo * ri)
{
  if (reg >= HX_ARM_REG_R0 && reg <= HX_ARM_REG_R12)
  {
    ri->meta = HOOX_ARM_MREG_R0 + (reg - HX_ARM_REG_R0);
    ri->width = 32;
    ri->index = ri->meta - HOOX_ARM_MREG_R0;
  }
  else if (reg == HX_ARM_REG_SP)
  {
    ri->meta = HOOX_ARM_MREG_SP;
    ri->width = 32;
    ri->index = ri->meta - HOOX_ARM_MREG_R0;
  }
  else if (reg == HX_ARM_REG_LR)
  {
    ri->meta = HOOX_ARM_MREG_LR;
    ri->width = 32;
    ri->index = ri->meta - HOOX_ARM_MREG_R0;
  }
  else if (reg == HX_ARM_REG_PC)
  {
    ri->meta = HOOX_ARM_MREG_PC;
    ri->width = 32;
    ri->index = ri->meta - HOOX_ARM_MREG_R0;
  }
  else if (reg >= HX_ARM_REG_S0 && reg <= HX_ARM_REG_S31)
  {
    ri->meta = HOOX_ARM_MREG_S0 + (reg - HX_ARM_REG_S0);
    ri->width = 32;
    ri->index = ri->meta - HOOX_ARM_MREG_S0;
  }
  else if (reg >= HX_ARM_REG_D0 && reg <= HX_ARM_REG_D31)
  {
    ri->meta = HOOX_ARM_MREG_D0 + (reg - HX_ARM_REG_D0);
    ri->width = 64;
    ri->index = ri->meta - HOOX_ARM_MREG_D0;
  }
  else if (reg >= HX_ARM_REG_Q0 && reg <= HX_ARM_REG_Q15)
  {
    ri->meta = HOOX_ARM_MREG_Q0 + (reg - HX_ARM_REG_Q0);
    ri->width = 128;
    ri->index = ri->meta - HOOX_ARM_MREG_Q0;
  }
  else
  {
    hx_assert_not_reached ();
  }
}
