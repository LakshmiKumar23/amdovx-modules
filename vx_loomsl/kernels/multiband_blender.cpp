/*
Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#define _CRT_SECURE_NO_WARNINGS
#include "kernels.h"
//#include "blend.h"

//! \brief The input validator callback.
static vx_status VX_CALLBACK multiband_blend_input_validator(vx_node node, vx_uint32 index)
{
	vx_status status = VX_ERROR_INVALID_PARAMETERS;
	// get reference for parameter at specified index
	vx_reference ref = avxGetNodeParamRef(node, index);
	ERROR_CHECK_OBJECT(ref);
	if (index == 0)
	{ // scalar of VX_TYPE_UINT32
		vx_enum itemtype = VX_TYPE_INVALID;
		ERROR_CHECK_STATUS(vxQueryScalar((vx_scalar)ref, VX_SCALAR_ATTRIBUTE_TYPE, &itemtype, sizeof(itemtype)));
		if (itemtype == VX_TYPE_UINT32) {
			status = VX_SUCCESS;
		}
		else {
			status = VX_ERROR_INVALID_DIMENSION;
			vxAddLogEntry((vx_reference)node, status, "ERROR: merge number of cameras should be UINT32 type\n");
		}
	}
	else if (index == 1)
	{ // scalar of VX_TYPE_UINT32
		vx_enum itemtype = VX_TYPE_INVALID;
		ERROR_CHECK_STATUS(vxQueryScalar((vx_scalar)ref, VX_SCALAR_ATTRIBUTE_TYPE, &itemtype, sizeof(itemtype)));
		if (itemtype == VX_TYPE_UINT32) {
			status = VX_SUCCESS;
		}
		else {
			status = VX_ERROR_INVALID_DIMENSION;
			vxAddLogEntry((vx_reference)node, status, "ERROR: merge valid_arr offs should be UINT32 type\n");
		}
	}
	else if (index == 2)
	{ // image of format RGBX(lowest level pyramid) or RGB4(laplacian)
		// check input image format and dimensions
		vx_uint32 input_width = 0, input_height = 0;
		vx_df_image input_format = VX_DF_IMAGE_VIRT;
		ERROR_CHECK_STATUS(vxQueryImage((vx_image)ref, VX_IMAGE_ATTRIBUTE_WIDTH, &input_width, sizeof(input_width)));
		ERROR_CHECK_STATUS(vxQueryImage((vx_image)ref, VX_IMAGE_ATTRIBUTE_HEIGHT, &input_height, sizeof(input_height)));
		ERROR_CHECK_STATUS(vxQueryImage((vx_image)ref, VX_IMAGE_ATTRIBUTE_FORMAT, &input_format, sizeof(input_format)));
		ERROR_CHECK_STATUS(vxReleaseImage((vx_image *)&ref));
		if ((input_format != VX_DF_IMAGE_RGBX) && (input_format != VX_DF_IMAGE_RGB4_AMD)){
			status = VX_ERROR_INVALID_TYPE;
			vxAddLogEntry((vx_reference)node, status, "ERROR: merge camera id selection for image %d should be an image of U016 type\n", index);
		}
		else {
			status = VX_SUCCESS;
		}
	}
	else if (index == 3)
	{ // image object of U008 type
		vx_df_image format = VX_DF_IMAGE_VIRT;
		ERROR_CHECK_STATUS(vxQueryImage((vx_image)ref, VX_IMAGE_ATTRIBUTE_FORMAT, &format, sizeof(format)));
		if (format == VX_DF_IMAGE_U8) {
			status = VX_SUCCESS;
		}
		else {
			status = VX_ERROR_INVALID_TYPE;
			vxAddLogEntry((vx_reference)node, status, "ERROR: merge weight image should be an image of U008 type\n");
		}
		ERROR_CHECK_STATUS(vxReleaseImage((vx_image *)&ref));
		return status;
	}
	else if (index == 4)
	{ // array object for offsets
		vx_size itemsize = 0;
		ERROR_CHECK_STATUS(vxQueryArray((vx_array)ref, VX_ARRAY_ATTRIBUTE_ITEMSIZE, &itemsize, sizeof(itemsize)));
		if (itemsize == sizeof(StitchBlendValidEntry)) {
			status = VX_SUCCESS;
		}
		else {
			status = VX_ERROR_INVALID_DIMENSION;
			vxAddLogEntry((vx_reference)node, status, "ERROR: warp array element (StitchBlendValidEntry) size should be 32 bytes\n");
		}
		ERROR_CHECK_STATUS(vxReleaseArray((vx_array *)&ref));
	}
	return status;
}

//! \brief The output validator callback.
static vx_status VX_CALLBACK multiband_blend_output_validator(vx_node node, vx_uint32 index, vx_meta_format meta)
{
	vx_status status = VX_ERROR_INVALID_PARAMETERS;
	if (index == 5)
	{ // image of format VX_DF_IMAGE_RGB4_AMD (48bit per pixel) of pyramid image
		// get image configuration
		vx_image image = (vx_image)avxGetNodeParamRef(node, 2);
		ERROR_CHECK_OBJECT(image);
		vx_uint32 input_width = 0, input_height = 0;
		ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_WIDTH, &input_width, sizeof(input_width)));
		ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_HEIGHT, &input_height, sizeof(input_height)));
		ERROR_CHECK_STATUS(vxReleaseImage(&image));
		image = (vx_image)avxGetNodeParamRef(node, index);
		ERROR_CHECK_OBJECT(image);
		vx_uint32 width = 0, height = 0;
		ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_WIDTH, &width, sizeof(width)));
		ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_HEIGHT, &height, sizeof(height)));
		ERROR_CHECK_STATUS(vxReleaseImage(&image));
		if (width != input_width)
		{ // pick default output width 
			width = input_width;
		}
		if (input_height != height)
		{ // pick default output height as the input map height
			height = input_height;
		}
		// set output image meta data
		vx_df_image format = VX_DF_IMAGE_RGB4_AMD;
		ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(meta, VX_IMAGE_ATTRIBUTE_WIDTH, &width, sizeof(width)));
		ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(meta, VX_IMAGE_ATTRIBUTE_HEIGHT, &height, sizeof(height)));
		ERROR_CHECK_STATUS(vxSetMetaFormatAttribute(meta, VX_IMAGE_ATTRIBUTE_FORMAT, &format, sizeof(format)));
		status = VX_SUCCESS;
	}
	return status;
}

//! \brief The kernel target support callback.
static vx_status VX_CALLBACK multiband_blend_query_target_support(vx_graph graph, vx_node node,
	vx_bool use_opencl_1_2,              // [input]  false: OpenCL driver is 2.0+; true: OpenCL driver is 1.2
	vx_uint32& supported_target_affinity // [output] must be set to AGO_TARGET_AFFINITY_CPU or AGO_TARGET_AFFINITY_GPU or (AGO_TARGET_AFFINITY_CPU | AGO_TARGET_AFFINITY_GPU)
	)
{
	supported_target_affinity = AGO_TARGET_AFFINITY_GPU;
	return VX_SUCCESS;
}

//! \brief The OpenCL code generator callback.
static vx_status VX_CALLBACK multiband_blend_opencl_codegen(
	vx_node node,                                  // [input] node
	const vx_reference parameters[],               // [input] parameters
	vx_uint32 num,                                 // [input] number of parameters
	bool opencl_load_function,                     // [input]  false: normal OpenCL kernel; true: reserved
	char opencl_kernel_function_name[64],          // [output] kernel_name for clCreateKernel()
	std::string& opencl_kernel_code,               // [output] string for clCreateProgramWithSource()
	std::string& opencl_build_options,             // [output] options for clBuildProgram()
	vx_uint32& opencl_work_dim,                    // [output] work_dim for clEnqueueNDRangeKernel()
	vx_size opencl_global_work[],                  // [output] global_work[] for clEnqueueNDRangeKernel()
	vx_size opencl_local_work[],                   // [output] local_work[] for clEnqueueNDRangeKernel()
	vx_uint32& opencl_local_buffer_usage_mask,     // [output] reserved: must be ZERO
	vx_uint32& opencl_local_buffer_size_in_bytes   // [output] reserved: must be ZERO
	)
{
	vx_size		wg_num;
	vx_uint32 numCam = 0;
	vx_uint32 width = 0, height = 0;
	vx_uint32 in_width = 0, in_height = 0;
	vx_scalar scalar = (vx_scalar)avxGetNodeParamRef(node, 0);			// input scalar - grayscale compute method
	ERROR_CHECK_OBJECT(scalar);
	ERROR_CHECK_STATUS(vxReadScalarValue(scalar, &numCam));
	ERROR_CHECK_STATUS(vxReleaseScalar(&scalar));
	vx_df_image in_format = VX_DF_IMAGE_VIRT;
	vx_image image = (vx_image)avxGetNodeParamRef(node, 2);				// input image
	ERROR_CHECK_OBJECT(image);
	ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_WIDTH, &in_width, sizeof(in_width)));
	ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_HEIGHT, &in_height, sizeof(in_height)));
	ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_FORMAT, &in_format, sizeof(in_format)));
	ERROR_CHECK_STATUS(vxReleaseImage(&image));
	vx_df_image out_format = VX_DF_IMAGE_VIRT;
	image = (vx_image)avxGetNodeParamRef(node, 5);				// output image
	ERROR_CHECK_OBJECT(image);
	ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_WIDTH, &width, sizeof(width)));
	ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_HEIGHT, &height, sizeof(height)));
	ERROR_CHECK_STATUS(vxQueryImage(image, VX_IMAGE_ATTRIBUTE_FORMAT, &out_format, sizeof(out_format)));
	ERROR_CHECK_STATUS(vxReleaseImage(&image));
	vx_array wg_offsets = (vx_array)avxGetNodeParamRef(node, 4);
	ERROR_CHECK_STATUS(vxQueryArray(wg_offsets, VX_ARRAY_ATTRIBUTE_CAPACITY, &wg_num, sizeof(wg_num)));
	ERROR_CHECK_STATUS(vxReleaseArray(&wg_offsets));
	// set kernel configuration
	strcpy(opencl_kernel_function_name, "multiband_blend");
	opencl_work_dim = 2;
	opencl_local_work[0] = 16;
	opencl_local_work[1] = 16;
	opencl_global_work[0] = wg_num*opencl_local_work[0];
	opencl_global_work[1] = opencl_local_work[1];
	vx_uint32 height1 = height;
	if (numCam)
		height1 = (vx_uint32)(height / numCam);

	char item[8192];
	sprintf(item,
		"#pragma OPENCL EXTENSION cl_amd_media_ops : enable\n"
		"#pragma OPENCL EXTENSION cl_amd_media_ops2 : enable\n"
		"\n"
		"float4 amd_unpack(uint src)\n"
		"{\n"
		"	return (float4)(amd_unpack0(src), amd_unpack1(src), amd_unpack2(src), amd_unpack3(src));\n"
		"}\n"
		"__kernel __attribute__((reqd_work_group_size(%d, %d, 1)))\n"
		"void %s(uint num_cam, uint arr_offs,\n"
		" 	uint ip_width, uint ip_height, __global uchar * ip_buf, uint ip_stride, uint ip_offset,\n"
		"	uint wt_width, uint wt_height, __global uchar * wt_buf, uint wt_stride, uint wt_offset,\n"
		"	__global uchar * pG_buf, uint pG_offs, uint pG_num,\n"
		"   uint op_width, uint op_height, __global uchar * op_buf, uint op_stride, uint op_offset)\n"
		"{\n"
		"	int grp_id = get_global_id(0)>>4, lx = get_local_id(0), ly = get_local_id(1);\n"
		"	if (grp_id < pG_num) {\n"
		"	uint2 offs = ((__global uint2 *)(pG_buf+pG_offs))[grp_id+arr_offs];\n"
		"	uint camera_id = offs.x & 0x1f; uint gx = (lx<<2) + ((offs.x >> 5) & 0x3FFF); uint gy = ly + (offs.x >> 19);\n"
		"   bool outputValid = (lx*4 < (offs.y & 0xFF)) & (ly <= (offs.y >> 8) ) ;\n"
		"	wt_buf += wt_offset + mad24(gy, wt_stride, gx);\n"
		"	op_buf  += op_offset + mad24(gy, op_stride, gx*6);\n"
		"	ip_buf += (camera_id * ip_stride*%d);\n"
		"	wt_buf += (camera_id * wt_stride*%d);\n"
		"	op_buf += (camera_id * op_stride*%d);\n"
		, opencl_local_work[0], opencl_local_work[1], opencl_kernel_function_name, height1, height1, height1);
	opencl_kernel_code = item;
	if (in_format == VX_DF_IMAGE_RGBX) {
		opencl_kernel_code +=
			"	if (outputValid){\n"
			"		uint4 RGB_in;\n"
			"		float4 f0;\n"
			"		ip_buf += ip_offset + mad24(gy, ip_stride, gx<<2);\n"
			"		RGB_in = *(__global uint4*)(ip_buf);\n"
			"		uchar4 wt_in  = *(__global uchar4*)(wt_buf);\n"
			"		f0 = amd_unpack(RGB_in.s0)*(float4)wt_in.s0;\n"
			"		f0 *= (float4)0.00392157f;	// normalize\n"
			"		*(__global short3*)(op_buf) = (short3)((short)f0.s0, (short)f0.s1, (short)f0.s2);\n"
			"		f0 = amd_unpack(RGB_in.s1)*(float4)wt_in.s1;\n"
			"		f0 *= (float4)0.00392157f;	// normalize\n"
			"		*(__global short3*)(op_buf+6) = (short3)((short)f0.s0, (short)f0.s1, (short)f0.s2);\n"
			"		f0 = amd_unpack(RGB_in.s2)*(float4)wt_in.s2;\n"
			"		f0 *= (float4)0.00392157f;	// normalize\n"
			"		*(__global short3*)(op_buf+12) = (short3)((short)f0.s0, (short)f0.s1, (short)f0.s2);\n"
			"		f0 = amd_unpack(RGB_in.s3)*(float4)wt_in.s3;\n"
			"		f0 *= (float4)0.00392157f;	// normalize\n"
			"		*(__global short3*)(op_buf+18) = (short3)((short)f0.s0, (short)f0.s1, (short)f0.s2);\n"
			"	}\n";
	}
	else
	{
		opencl_kernel_code +=
			"	if (outputValid){\n"
			"		uint4 RGB_in0; uint2 RGB_in1;\n"
			"		float3 RGB_out;\n"
			"		float8 f0; float4 f1;\n"
			"		ip_buf += ip_offset + mad24(gy, ip_stride, gx*6);\n"
			"		RGB_in0 = *(__global uint4*)(ip_buf);\n"
			"		RGB_in1 = *(__global uint2*)(ip_buf+16);\n"
			"		uchar4 wt_in  = *(__global uchar4*)(wt_buf);\n"
			"		f0  = convert_float8(as_short8(RGB_in0));\n"
			"		f1  = convert_float4(as_short4(RGB_in1));\n"
			"		RGB_out = f0.s012*(float3)wt_in.s0*(float3)0.00392157f;\n"
			"		*(__global short3*)(op_buf) = convert_short3(RGB_out);\n"
			"		RGB_out = f0.s345*(float3)wt_in.s1*(float3)0.00392157f;\n"
			"		*(__global short3*)(op_buf+6) = convert_short3(RGB_out);\n"
			"		RGB_out = (float3)(f0.s67,f1.s0) *(float3)wt_in.s2*(float3)0.00392157f;\n"
			"		*(__global short3*)(op_buf+12) = convert_short3(RGB_out);\n"
			"		RGB_out = f1.s123*(float3)wt_in.s3*(float3)0.00392157f;\n"
			"		*(__global short3*)(op_buf+18) = convert_short3(RGB_out);\n"
			"	}\n";
	}
	opencl_kernel_code +=
		"	}\n"
		" }\n";
	return VX_SUCCESS;
}

//! \brief The kernel execution.
static vx_status VX_CALLBACK multiband_blend_kernel(vx_node node, const vx_reference * parameters, vx_uint32 num)
{
	return VX_ERROR_NOT_IMPLEMENTED;
}

//! \brief The OpenCL global work updater callback.
static vx_status VX_CALLBACK multiband_blend_opencl_global_work_update(
	vx_node node,                                  // [input] node
	const vx_reference parameters[],               // [input] parameters
	vx_uint32 num,                                 // [input] number of parameters
	vx_uint32 opencl_work_dim,                     // [input] work_dim for clEnqueueNDRangeKernel()
	vx_size opencl_global_work[],                  // [output] global_work[] for clEnqueueNDRangeKernel()
	const vx_size opencl_local_work[]              // [input] local_work[] for clEnqueueNDRangeKernel()
	)
{
	vx_uint32 arr_offset;
	vx_size arr_numitems = 0;
	vx_array arr = (vx_array)avxGetNodeParamRef(node, 4);				// input array
	ERROR_CHECK_OBJECT(arr);
	vx_scalar scalar = (vx_scalar)avxGetNodeParamRef(node, 1);			// input scalar - array_offset
	ERROR_CHECK_OBJECT(scalar);
	ERROR_CHECK_STATUS(vxReadScalarValue(scalar, &arr_offset));
	ERROR_CHECK_STATUS(vxReleaseScalar(&scalar));
	StitchBlendValidEntry *pBlendArr = nullptr;
	vx_size stride_blend_arr = sizeof(StitchBlendValidEntry);
	ERROR_CHECK_STATUS(vxAccessArrayRange(arr, arr_offset - 1, arr_offset, &stride_blend_arr, (void **)&pBlendArr, VX_READ_ONLY));
	arr_numitems = *((vx_uint32 *)pBlendArr);
	ERROR_CHECK_STATUS(vxCommitArrayRange(arr, arr_offset - 1, arr_offset, pBlendArr));
	ERROR_CHECK_STATUS(vxReleaseArray(&arr));
	opencl_global_work[0] = arr_numitems*opencl_local_work[0];
	opencl_global_work[1] = opencl_local_work[1];
	return VX_SUCCESS;
}

//! \brief The exposure_comp_applygains kernel publisher.
vx_status multiband_blend_publish(vx_context context)
{
	// add kernel to the context with callbacks
	vx_kernel kernel = vxAddKernel(context, "com.amd.loomsl.multiband_blend",
		AMDOVX_KERNEL_STITCHING_MULTIBAND_BLEND,
		multiband_blend_kernel,
		6,
		multiband_blend_input_validator,
		multiband_blend_output_validator,
		nullptr,
		nullptr);
	ERROR_CHECK_OBJECT(kernel);
	amd_kernel_query_target_support_f query_target_support_f = multiband_blend_query_target_support;
	amd_kernel_opencl_codegen_callback_f opencl_codegen_callback_f = multiband_blend_opencl_codegen;
	amd_kernel_opencl_global_work_update_callback_f opencl_global_work_update_callback_f = multiband_blend_opencl_global_work_update;
	ERROR_CHECK_STATUS(vxSetKernelAttribute(kernel, VX_KERNEL_ATTRIBUTE_AMD_QUERY_TARGET_SUPPORT, &query_target_support_f, sizeof(query_target_support_f)));
	ERROR_CHECK_STATUS(vxSetKernelAttribute(kernel, VX_KERNEL_ATTRIBUTE_AMD_OPENCL_CODEGEN_CALLBACK, &opencl_codegen_callback_f, sizeof(opencl_codegen_callback_f)));
	ERROR_CHECK_STATUS(vxSetKernelAttribute(kernel, VX_KERNEL_ATTRIBUTE_AMD_OPENCL_GLOBAL_WORK_UPDATE_CALLBACK, &opencl_global_work_update_callback_f, sizeof(opencl_global_work_update_callback_f)));
	// set kernel parameters
	ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 0, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
	ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 1, VX_INPUT, VX_TYPE_SCALAR, VX_PARAMETER_STATE_REQUIRED));
	ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 2, VX_INPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED));
	ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 3, VX_INPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED));
	ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 4, VX_INPUT, VX_TYPE_ARRAY, VX_PARAMETER_STATE_REQUIRED));
	ERROR_CHECK_STATUS(vxAddParameterToKernel(kernel, 5, VX_OUTPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED));
	// finalize and release kernel object
	ERROR_CHECK_STATUS(vxFinalizeKernel(kernel));
	ERROR_CHECK_STATUS(vxReleaseKernel(&kernel));
	return VX_SUCCESS;
}

vx_uint32 Compute_StitchBlendArraySize(int width, int height, int num_camera, int num_bands, vx_uint32 offset[])
{
	vx_uint32 totalCount = 0;
	for (int level = 0; level < num_bands; level++){
		offset[level] = 1 + totalCount;
		vx_uint32 count = 1 + (((width >> level) + 63) >> 6) * (((height >> level) + 15) >> 4) * num_camera;
		totalCount += count;
	}
	return totalCount;
}

// helper function for calculating offset tables for blend
vx_status Compute_StitchMultiBandCalcValidEntry(vx_rectangle_t *pValid_roi, vx_array blendOffs, int numCameras, int numBands, int width, int height)
{
	vx_uint32 *array_offset = new vx_uint32[numBands];
	vx_rectangle_t *pRoi_rect = new vx_rectangle_t[numCameras];
	Compute_StitchBlendArraySize(width, height, numCameras, numBands, array_offset);
	vx_size max_size;
	StitchBlendValidEntry blend_entry = { 0 };
	ERROR_CHECK_STATUS(vxQueryArray(blendOffs, VX_ARRAY_ATTRIBUTE_CAPACITY, &max_size, sizeof(max_size)));
	ERROR_CHECK_STATUS(vxAddArrayItems(blendOffs, max_size, &blend_entry, 0));		// stride 0 will make all the elements initialized to 0
	StitchBlendValidEntry *pBlendArr = nullptr;
	vx_size stride_blend_arr = sizeof(StitchBlendValidEntry);
	ERROR_CHECK_STATUS(vxAccessArrayRange(blendOffs, 0, max_size, &stride_blend_arr, (void **)&pBlendArr, VX_READ_AND_WRITE));
	int align = (1 << (numBands-1));
	int border = align * 2;			// to make sure all desired pixels are filtered
	vx_int32 x1, y1, x2, y2;
	for (int i = 0; i < numCameras; i++){
		vx_rectangle_t *pRect = pValid_roi + i;
		x1 = pRect->start_x, x2 = pRect->end_x;
		y1 = pRect->start_y, y2 = pRect->end_y;
		x1 -= border; y1 -= border;
		x2 += border; y2 += border;
		if (x1 < 0) x1 = 0;
		if (y1 < 0) y1 = 0;
		if (x2 >= width) x2 = width - 1;
		if (y2 >= height) y2 = height - 1;
		x1 = x1 & ~(align - 1);
		if (x1 < 0) x1 = 0;
		y1 = y1 & ~(align - 1);
		if (y1 < 0) y1 = 0;
		x2 = (x2 + align - 1) & ~(align - 1);
		y2 = (y2 + align - 1) & ~(align - 1);
		if (x2 >= width) x2 = width - 1;
		if (y2 >= height) y2 = height - 1;
		pRoi_rect[i] = { x1, y1, x2, y2 };
	}

	for (int level = 0; level < numBands; level++){
		StitchBlendValidEntry *entry_start = &pBlendArr[array_offset[level] - 1];
		StitchBlendValidEntry *entry = entry_start+1;
		int num_entries = 0;
		for (int i = 0; i < numCameras; i++){
			vx_rectangle_t *pRect = pRoi_rect + i;
			x1 = pRect->start_x, x2 = pRect->end_x;
			y1 = pRect->start_y, y2 = pRect->end_y;
			x1 >>= level, y1 >>= level;
			x2 >>= level, y2 >>= level;
			for (int y = y1; y < y2; y += 16){
				for (int x = x1 & ~15; x < x2; x += 64){
					entry->camId = i;
					entry->dstX = x;
					entry->dstY = y;
					entry->end_x = ((x + 63) > x2) ? (x2 - x) : 63;
					entry->end_y = ((y + 15) > y2) ? (y2 - y) : 15;
					entry->start_x = (x < x1) ? (x1 - x) : 0;
					entry->start_y = 0;
					entry++;
				}
			}
		}
		*((vx_uint32 *)entry_start) = (vx_uint32)(entry - entry_start - 1);
	}
	ERROR_CHECK_STATUS(vxCommitArrayRange(blendOffs, 0, max_size, pBlendArr));
	delete array_offset;
	delete pRoi_rect;
	return VX_SUCCESS;
}
