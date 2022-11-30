/**
  * Copyright (C) 2020 Salvo Limited Hong Kong
  * 
  *  Licensed under the Apache License, Version 2.0 (the "License");
  *  you may not use this file except in compliance with the License.
  *  You may obtain a copy of the License at
  *
  *      http://www.apache.org/licenses/LICENSE-2.0
  *
  *  Unless required by applicable law or agreed to in writing, software
  *  distributed under the License is distributed on an "AS IS" BASIS,
  *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  *  See the License for the specific language governing permissions and
  *  limitations under the License.
  *
***/
#include "../include/LoggingHelper.hpp"
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MAIN
#include <boost/test/included/unit_test.hpp>
BOOST_AUTO_TEST_CASE( LoggingHelperTest )
{
  char buf[1024] __attribute__((__may_alias__));
  char *worldcpy = strdup("Now is the time for all good men to come to the aid of their country");
  LoggingHelper::Printer::createPrinter<sizeof(buf)>(stdout, buf, "%.4f, Hello there %s %d\n", 1.0/3, worldcpy, 5);
  worldcpy[0] = '!';
  free(worldcpy);
  fprintf(stderr, "Now I'm calling the printer:\n");
  auto* f __attribute__((__may_alias__)) = reinterpret_cast<LoggingHelper::Printer*>(buf);
  f->print();
}
