/*
 * Copyright (c) 2020 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <dispatch/audio.h>
#include <dispatch/camera.h>
#include <dispatch/register.h>
#include <dispatch/screen.h>

#include <dispatch/libraries/sysutils/functions.h>
#include <dispatch/libraries/egl/egl.h>
#include <dispatch/libraries/gles1/gles1.h>

namespace eka2l1::dispatch {
    const eka2l1::dispatch::func_map dispatch_funcs = {
        BRIDGE_REGISTER_DISPATCHER(1, update_screen),
        BRIDGE_REGISTER_DISPATCHER(2, fast_blit),
        BRIDGE_REGISTER_DISPATCHER(3, wait_vsync),
        BRIDGE_REGISTER_DISPATCHER(4, cancel_wait_vsync),
        BRIDGE_REGISTER_DISPATCHER(5, flexible_post),
        BRIDGE_REGISTER_DISPATCHER(0x20, eaudio_player_inst),
        BRIDGE_REGISTER_DISPATCHER(0x21, eaudio_player_notify_any_done),
        BRIDGE_REGISTER_DISPATCHER(0x22, eaudio_player_supply_url),
        BRIDGE_REGISTER_DISPATCHER(0x23, eaudio_player_supply_buffer),
        BRIDGE_REGISTER_DISPATCHER(0x24, eaudio_player_set_volume),
        BRIDGE_REGISTER_DISPATCHER(0x25, eaudio_player_get_volume),
        BRIDGE_REGISTER_DISPATCHER(0x26, eaudio_player_max_volume),
        BRIDGE_REGISTER_DISPATCHER(0x27, eaudio_player_play),
        BRIDGE_REGISTER_DISPATCHER(0x28, eaudio_player_stop),
        BRIDGE_REGISTER_DISPATCHER(0x2A, eaudio_player_cancel_notify_done),
        BRIDGE_REGISTER_DISPATCHER(0x30, eaudio_player_set_repeats),
        BRIDGE_REGISTER_DISPATCHER(0x31, eaudio_player_destroy),
        BRIDGE_REGISTER_DISPATCHER(0x32, eaudio_player_get_dest_sample_rate),
        BRIDGE_REGISTER_DISPATCHER(0x33, eaudio_player_set_dest_sample_rate),
        BRIDGE_REGISTER_DISPATCHER(0x34, eaudio_player_get_dest_channel_count),
        BRIDGE_REGISTER_DISPATCHER(0x35, eaudio_player_set_dest_channel_count),
        BRIDGE_REGISTER_DISPATCHER(0x36, eaudio_player_set_dest_encoding),
        BRIDGE_REGISTER_DISPATCHER(0x37, eaudio_player_get_dest_encoding),
        BRIDGE_REGISTER_DISPATCHER(0x38, eaudio_player_set_dest_container_format),
        BRIDGE_REGISTER_DISPATCHER(0x40, eaudio_dsp_out_stream_create),
        BRIDGE_REGISTER_DISPATCHER(0x41, eaudio_dsp_out_stream_write),
        BRIDGE_REGISTER_DISPATCHER(0x42, eaudio_dsp_stream_set_properties),
        BRIDGE_REGISTER_DISPATCHER(0x43, eaudio_dsp_stream_start),
        BRIDGE_REGISTER_DISPATCHER(0x44, eaudio_dsp_stream_stop),
        BRIDGE_REGISTER_DISPATCHER(0x47, eaudio_dsp_out_stream_set_volume),
        BRIDGE_REGISTER_DISPATCHER(0x48, eaudio_dsp_out_stream_max_volume),
        BRIDGE_REGISTER_DISPATCHER(0x49, eaudio_dsp_stream_notify_buffer_ready),
        BRIDGE_REGISTER_DISPATCHER(0x4B, eaudio_dsp_stream_destroy),
        BRIDGE_REGISTER_DISPATCHER(0x4C, eaudio_dsp_out_stream_volume),
        BRIDGE_REGISTER_DISPATCHER(0x4F, eaudio_dsp_stream_bytes_rendered),
        BRIDGE_REGISTER_DISPATCHER(0x50, eaudio_dsp_stream_position),
        BRIDGE_REGISTER_DISPATCHER(0x52, eaudio_dsp_stream_notify_buffer_ready_cancel),
        BRIDGE_REGISTER_DISPATCHER(0x53, eaudio_dsp_stream_reset_stat),
        BRIDGE_REGISTER_DISPATCHER(0x60, ecam_number_of_cameras),
        BRIDGE_REGISTER_DISPATCHER(0x1000, sysutils::sysstartup_get_state),
        BRIDGE_REGISTER_DISPATCHER(0x1100, egl_choose_config_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1101, egl_copy_buffers_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1102, egl_create_context_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1103, egl_create_pbuffer_surface_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1104, egl_create_pixmap_surface_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1105, egl_create_window_surface_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1106, egl_destroy_context_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1107, egl_destroy_surface_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1108, egl_get_config_attrib_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1109, egl_get_configs_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x110A, egl_get_current_context_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x110B, egl_get_current_display_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x110C, egl_get_current_surface_emu),
        BRIDGE_REGISTER_DISPATCHER(0x110D, egl_get_display_emu),
        BRIDGE_REGISTER_DISPATCHER(0x110E, egl_get_error),
        //BRIDGE_REGISTER_DISPATCHER(0x110F, egl_get_proc_address_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1110, egl_initialize_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1111, egl_make_current_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1112, egl_query_context_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1113, egl_query_string_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1114, egl_query_surface_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1115, egl_swap_buffers_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1116, egl_terminate_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1117, egl_wait_gl_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1118, egl_wait_native_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1119, gl_active_texture_emu),
        BRIDGE_REGISTER_DISPATCHER(0x111A, gl_alpha_func_emu),
        BRIDGE_REGISTER_DISPATCHER(0x111B, gl_alpha_func_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x111C, gl_bind_texture_emu),
        BRIDGE_REGISTER_DISPATCHER(0x111D, gl_blend_func_emu),
        BRIDGE_REGISTER_DISPATCHER(0x111E, gl_clear_emu),
        BRIDGE_REGISTER_DISPATCHER(0x111F, gl_clear_color_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1120, gl_clear_colorx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1121, gl_clear_depthf_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1122, gl_clear_depthx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1123, gl_clear_stencil),
        BRIDGE_REGISTER_DISPATCHER(0x1124, gl_client_active_texture_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1125, gl_color_4f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1126, gl_color_4x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1127, gl_color_mask_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1128, gl_color_pointer_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1129, gl_compressed_tex_image_2d_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x112A, gl_compressed_tex_sub_image_2d_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x112B, gl_copy_tex_image_2d),
        //BRIDGE_REGISTER_DISPATCHER(0x112C, gl_copy_tex_sub_image_2d),
        BRIDGE_REGISTER_DISPATCHER(0x112D, gl_cull_face_emu),
        BRIDGE_REGISTER_DISPATCHER(0x112E, gl_delete_textures_emu),
        BRIDGE_REGISTER_DISPATCHER(0x112F, gl_depth_func_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1130, gl_depth_mask_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1131, gl_depth_range_f_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1132, gl_depth_range_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1133, gl_disable_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1134, gl_disable_client_state_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1135, gl_draw_arrays_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1136, gl_draw_elements_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1137, gl_enable_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1138, gl_enable_client_state_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1139, gl_finish_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x113A, gl_flush_emu),
        BRIDGE_REGISTER_DISPATCHER(0x113B, gl_fog_f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x113C, gl_fog_fv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x113D, gl_fog_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x113E, gl_fog_xv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x113F, gl_front_face_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1140, gl_frustumf_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1141, gl_frustumx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1142, gl_gen_textures_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1143, gl_get_error_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1144, gl_get_integerv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1145, gl_get_string_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1146, gl_hint_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1147, gl_light_model_f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1148, gl_light_model_fv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1149, gl_light_model_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x114A, gl_light_model_xv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x114B, gl_light_f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x114C, gl_light_fv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x114D, gl_light_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x114E, gl_light_xv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x114F, gl_line_width_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1150, gl_line_widthx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1151, gl_load_identity_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1152, gl_load_matrixf_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1153, gl_load_matrixx_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1154, gl_logic_op_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1155, gl_material_f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1156, gl_material_fv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1157, gl_material_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1158, gl_material_xv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1159, gl_matrix_mode_emu),
        BRIDGE_REGISTER_DISPATCHER(0x115A, gl_mult_matrixf_emu),
        BRIDGE_REGISTER_DISPATCHER(0x115B, gl_mult_matrixx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x115C, gl_multi_tex_coord_4f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x115D, gl_multi_tex_coord_4x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x115E, gl_normal_3f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x115F, gl_normal_3x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1160, gl_normal_pointer_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1161, gl_orthof_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1162, gl_orthox_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1163, gl_pixel_storei_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1164, gl_point_size_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1165, gl_point_sizex_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1166, gl_polygon_offset_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1167, gl_polygon_offsetx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1168, gl_pop_matrix_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1169, gl_push_matrix_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x116A, gl_query_matrixx_oes_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x116B, gl_read_pixels_emu),
        BRIDGE_REGISTER_DISPATCHER(0x116C, gl_rotatef_emu),
        BRIDGE_REGISTER_DISPATCHER(0x116D, gl_rotatex_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x116E, gl_sample_coverage_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x116F, gl_sample_coveragex_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1170, gl_scalef_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1171, gl_scalex_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1172, gl_scissor_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1173, gl_shade_model_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1174, gl_stencil_func_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1175, gl_stencil_mask_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1176, gl_stencil_op_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1177, gl_texcoord_pointer_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1178, gl_tex_envf_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1179, gl_tex_envfv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x117A, gl_tex_envx_emu),
        BRIDGE_REGISTER_DISPATCHER(0x117B, gl_tex_envxv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x117C, gl_tex_image_2d_emu),
        BRIDGE_REGISTER_DISPATCHER(0x117D, gl_tex_parameter_f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x117E, gl_tex_parameter_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x117F, gl_tex_sub_image_2d_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1180, gl_translatef_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1181, gl_translatex_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1182, gl_vertex_pointer_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1183, gl_viewport_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1184, egl_swap_interval_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1185, gl_bind_buffer_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1186, gl_buffer_data_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1187, gl_buffer_sub_data_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1188, gl_clip_plane_f_emu),
        BRIDGE_REGISTER_DISPATCHER(0x1189, gl_clip_plane_x_emu),
        BRIDGE_REGISTER_DISPATCHER(0x118A, gl_color_4ub_emu),
        BRIDGE_REGISTER_DISPATCHER(0x118B, gl_delete_buffers_emu),
        BRIDGE_REGISTER_DISPATCHER(0x118C, gl_gen_buffers_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x118D, gl_get_boolean_v_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x118E, gl_get_buffer_parmaeter_iv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x118F, gl_get_clip_plane_f_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1190, gl_get_clip_plane_x_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1191, gl_get_fixed_v_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1192, gl_get_float_v_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1193, gl_get_light_fv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1194, gl_get_light_xv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1195, gl_get_material_fv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1196, gl_get_material_xv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1197, gl_get_pointer_v_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1198, gl_get_tex_env_fv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x1199, gl_get_tex_env_iv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x119A, gl_get_tex_env_xv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x119B, gl_get_tex_parameter_fv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x119C, gl_get_tex_parameter_iv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x119D, gl_get_tex_parameter_xv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x119E, gl_is_buffer_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x119F, gl_is_enabled_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11A0, gl_is_texture_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11A1, gl_point_parameter_f_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11A2, gl_point_parameter_fv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11A3, gl_point_parameter_x_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11A4, gl_point_parameter_xv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11A5, gl_point_size_pointer_oes_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11A6, gl_tex_envi_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11A7, gl_tex_enviv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11A8, gl_tex_parameter_fv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11A9, gl_tex_parameter_i_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11AA, gl_tex_parameter_iv_emu),
        BRIDGE_REGISTER_DISPATCHER(0x11AB, gl_tex_parameter_xv_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11AC, egl_bind_tex_image_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11AD, egl_release_tex_image_emu),
        //BRIDGE_REGISTER_DISPATCHER(0x11AE, egl_surface_attrib_emu),
    };
}