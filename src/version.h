// Firmware identity, shown on the Signals page and in the serial status/boot log.
// TERRARIUM_GIT_SHA is injected at build time by scripts/gitsha.py ("-dirty" when the
// working tree has uncommitted changes), so a running build can be told apart from a stored one.
#pragma once

#define TERRARIUM_VERSION "1.0.1"

#ifndef TERRARIUM_GIT_SHA
#define TERRARIUM_GIT_SHA "nogit"
#endif
