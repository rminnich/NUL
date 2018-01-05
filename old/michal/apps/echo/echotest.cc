/**
 * @file
 * Test application for echo service.
 *
 * Copyright (C) 2011, 2012 Michal Sojka <sojka@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of Vancouver.nova.
 *
 * Vancouver.nova is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * Vancouver.nova is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include "wvprogram.h"
#include "service_echo.h"

class EchoTest : public WvProgram
{
public:
  void wvrun(Utcb *utcb, Hip *hip)
  {
    EchoProtocol *echo = new EchoProtocol(this);

    WVPASSEQ(echo->echo(*myutcb(), 42), 42U);
  }
};

ASMFUNCS(EchoTest, WvTest)
