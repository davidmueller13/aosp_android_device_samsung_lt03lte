#!/bin/sh

# Copyright 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

source ../../../common/clear-factory-images-variables.sh
BUILD=1433121
DEVICE=lt03lte
PRODUCT=lt03lte
VERSION=lrw73l
SRCPREFIX=signed-
BOOTLOADER=moto-apq8084-70.39
RADIO=d4.0-9625-02.48
source ../../../common/generate-factory-images-common.sh
