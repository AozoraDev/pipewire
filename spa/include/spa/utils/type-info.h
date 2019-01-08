/* Simple Plugin API
 *
 * Copyright © 2018 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __SPA_TYPE_INFO_H__
#define __SPA_TYPE_INFO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <spa/utils/defs.h>

#ifndef SPA_TYPE_ROOT
#define SPA_TYPE_ROOT	spa_types
#endif

static inline bool spa_type_is_a(const char *type, const char *parent)
{
	return type != NULL && parent != NULL && strncmp(type, parent, strlen(parent)) == 0;
}

#include <spa/utils/type.h>

/* base for parameter object enumerations */
#define SPA_TYPE__Direction		SPA_TYPE_ENUM_BASE "Direction"
#define SPA_TYPE_DIRECTION_BASE		SPA_TYPE__Direction ":"

static const struct spa_type_info spa_type_direction[] = {
	{ SPA_DIRECTION_INPUT, SPA_TYPE_DIRECTION_BASE "Input", SPA_TYPE_Int, NULL  },
	{ SPA_DIRECTION_OUTPUT, SPA_TYPE_DIRECTION_BASE "Output", SPA_TYPE_Int, NULL  },
	{ 0, NULL, 0, NULL }
};

#include <spa/monitor/type-info.h>
#include <spa/node/type-info.h>
#include <spa/param/type-info.h>
#include <spa/control/type-info.h>

/* base for parameter object enumerations */
#define SPA_TYPE__Choice		SPA_TYPE_ENUM_BASE "Choice"
#define SPA_TYPE_CHOICE_BASE		SPA_TYPE__Choice ":"

static const struct spa_type_info spa_type_choice[] = {
	{ SPA_CHOICE_None, SPA_TYPE_CHOICE_BASE "None", SPA_TYPE_Int, NULL  },
	{ SPA_CHOICE_Range, SPA_TYPE_CHOICE_BASE "Range", SPA_TYPE_Int, NULL  },
	{ SPA_CHOICE_Step, SPA_TYPE_CHOICE_BASE "Step", SPA_TYPE_Int, NULL  },
	{ SPA_CHOICE_Enum, SPA_TYPE_CHOICE_BASE "Enum", SPA_TYPE_Int, NULL  },
	{ SPA_CHOICE_Flags, SPA_TYPE_CHOICE_BASE "Flags", SPA_TYPE_Int, NULL  },
	{ 0, NULL, 0, NULL }
};

static const struct spa_type_info spa_types[] = {
        /* Basic types */
	{ SPA_TYPE_START, SPA_TYPE_BASE, SPA_TYPE_START, NULL },
	{ SPA_TYPE_None, SPA_TYPE_BASE "None", SPA_TYPE_None, NULL },
	{ SPA_TYPE_Bool, SPA_TYPE_BASE "Bool", SPA_TYPE_Bool, NULL },
	{ SPA_TYPE_Id, SPA_TYPE_BASE "Id", SPA_TYPE_Int, NULL },
	{ SPA_TYPE_Int, SPA_TYPE_BASE "Int", SPA_TYPE_Int, NULL },
	{ SPA_TYPE_Long, SPA_TYPE_BASE "Long", SPA_TYPE_Long, NULL },
	{ SPA_TYPE_Float, SPA_TYPE_BASE "Float", SPA_TYPE_Float, NULL },
	{ SPA_TYPE_Double, SPA_TYPE_BASE "Double", SPA_TYPE_Double, NULL },
	{ SPA_TYPE_String, SPA_TYPE_BASE "String", SPA_TYPE_String, NULL },
	{ SPA_TYPE_Bytes, SPA_TYPE_BASE "Bytes", SPA_TYPE_Bytes, NULL },
	{ SPA_TYPE_Rectangle, SPA_TYPE_BASE "Rectangle", SPA_TYPE_Rectangle, NULL },
	{ SPA_TYPE_Fraction, SPA_TYPE_BASE "Fraction", SPA_TYPE_Fraction, NULL },
	{ SPA_TYPE_Bitmap, SPA_TYPE_BASE "Bitmap", SPA_TYPE_Bitmap, NULL },
	{ SPA_TYPE_Array, SPA_TYPE_BASE "Array", SPA_TYPE_Array, NULL },
	{ SPA_TYPE_Pod, SPA_TYPE__Pod, SPA_TYPE_Pod, NULL },
	{ SPA_TYPE_Struct, SPA_TYPE__Struct, SPA_TYPE_Pod, NULL },
	{ SPA_TYPE_Object, SPA_TYPE__Object, SPA_TYPE_Pod, NULL },
	{ SPA_TYPE_Sequence, SPA_TYPE_POD_BASE "Sequence", SPA_TYPE_Pod, NULL },
	{ SPA_TYPE_Pointer, SPA_TYPE__Pointer, SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_Fd, SPA_TYPE_BASE "Fd", SPA_TYPE_Fd, NULL },
	{ SPA_TYPE_Choice, SPA_TYPE_POD_BASE "Choice", SPA_TYPE_Pod, NULL },

	{ SPA_TYPE_POINTER_START, SPA_TYPE__Pointer, SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_POINTER_Buffer, SPA_TYPE_POINTER_BASE "Buffer", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_POINTER_Meta, SPA_TYPE_POINTER_BASE "Meta", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_POINTER_Dict, SPA_TYPE_POINTER_BASE "Dict", SPA_TYPE_Pointer, NULL },

	{ SPA_TYPE_INTERFACE_START, SPA_TYPE__Interface, SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_Handle, SPA_TYPE_INTERFACE_BASE "Handle", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_HandleFactory, SPA_TYPE_INTERFACE_BASE "HandleFactory", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_Log, SPA_TYPE_INTERFACE_BASE "Log", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_Loop, SPA_TYPE_INTERFACE_BASE "Loop", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_LoopControl, SPA_TYPE_INTERFACE_BASE "LoopControl", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_LoopUtils, SPA_TYPE_INTERFACE_BASE "LoopUtils", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_DataLoop, SPA_TYPE_INTERFACE_BASE "DataLoop", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_MainLoop, SPA_TYPE_INTERFACE_BASE "MainLoop", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_DBus, SPA_TYPE_INTERFACE_BASE "DBus", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_Monitor, SPA_TYPE_INTERFACE_BASE "Monitor", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_Node, SPA_TYPE_INTERFACE_BASE "Node", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_Device, SPA_TYPE_INTERFACE_BASE "Device", SPA_TYPE_Pointer, NULL },
	{ SPA_TYPE_INTERFACE_CPU, SPA_TYPE_INTERFACE_BASE "CPU", SPA_TYPE_Pointer, NULL },

	{ SPA_TYPE_EVENT_START, SPA_TYPE__Event, SPA_TYPE_Object, NULL },
	{ SPA_TYPE_EVENT_Monitor, SPA_TYPE_EVENT_BASE "Monitor", SPA_TYPE_Object, spa_type_monitor_event },
	{ SPA_TYPE_EVENT_Node, SPA_TYPE_EVENT_BASE "Node", SPA_TYPE_Object, spa_type_node_event },

	{ SPA_TYPE_COMMAND_START, SPA_TYPE__Command, SPA_TYPE_Object, NULL },
	{ SPA_TYPE_COMMAND_Node, SPA_TYPE_COMMAND_BASE "Node", SPA_TYPE_Object, spa_type_node_command },

	{ SPA_TYPE_OBJECT_START, SPA_TYPE__Object, SPA_TYPE_Object, NULL },
	{ SPA_TYPE_OBJECT_MonitorItem, SPA_TYPE__MonitorItem, SPA_TYPE_Object, spa_type_monitor_item },
	{ SPA_TYPE_OBJECT_ParamList, SPA_TYPE_PARAM__List, SPA_TYPE_Object, spa_type_param_list, },
	{ SPA_TYPE_OBJECT_PropInfo, SPA_TYPE__PropInfo, SPA_TYPE_Object, spa_type_prop_info, },
	{ SPA_TYPE_OBJECT_Props, SPA_TYPE__Props, SPA_TYPE_Object, spa_type_props },
	{ SPA_TYPE_OBJECT_Format, SPA_TYPE__Format, SPA_TYPE_Object, spa_type_format },
	{ SPA_TYPE_OBJECT_ParamBuffers, SPA_TYPE_PARAM__Buffers, SPA_TYPE_Object, spa_type_param_buffers, },
	{ SPA_TYPE_OBJECT_ParamMeta, SPA_TYPE_PARAM__Meta, SPA_TYPE_Object, spa_type_param_meta },
	{ SPA_TYPE_OBJECT_ParamIO, SPA_TYPE_PARAM__IO, SPA_TYPE_Object, spa_type_param_io },
	{ SPA_TYPE_OBJECT_ParamProfile, SPA_TYPE_PARAM__Profile, SPA_TYPE_Object, spa_type_param_profile },

	{ 0, NULL, 0, NULL }
};

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* __SPA_TYPE_INFO_H__ */
