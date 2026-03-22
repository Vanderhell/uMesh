/*
 * Arduino library shim.
 *
 * Arduino's new-style library loader adds src/ to the include path.
 * This file lets sketches do #include "umesh.h" and find the canonical
 * public header at include/umesh.h.
 */
#include "../include/umesh.h"
