# Copyright 2019 NXP
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/gpt_hw.c \
	$(LOCAL_DIR)/gpt.c

MODULE_DEFINES += FSL_SDK_DISABLE_IRQ=1 \
	FSL_SDK_DISABLE_DRIVER_CLOCK_CONTROL=1


include make/module.mk
