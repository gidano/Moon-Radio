#pragma once

#include <Arduino.h>

struct Station {
  String name;
  String url;
  String logoName{"nologo"};
  String homepage;
};
