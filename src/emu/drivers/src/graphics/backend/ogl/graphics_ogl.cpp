/*
 * Copyright (c) 2019 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
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

#include <common/algorithm.h>
#include <common/log.h>
#include <common/platform.h>
#include <common/rgb.h>
#include <fstream>
#include <sstream>

#include <drivers/graphics/backend/ogl/common_ogl.h>
#include <drivers/graphics/backend/ogl/graphics_ogl.h>
#include <drivers/graphics/backend/ogl/buffer_ogl.h>
#include <glad/glad.h>

#if EKA2L1_PLATFORM(ANDROID)
#include <EGL/egl.h>
#endif

#define IMGUI_IMPL_OPENGL_LOADER_GLAD

namespace eka2l1::drivers {
    static void gl_post_callback_for_error(const char *name, void *funcptr, int len_args, ...) {
        GLenum error_code;
        error_code = glad_glGetError();

        if (error_code != GL_NO_ERROR) {
            LOG_ERROR(DRIVER_GRAPHICS, "{} encounters error {}", name, error_code);
        }
    }

    void init_gl_graphics_library(graphics::gl_context::mode api) {
        switch (api) {
            case graphics::gl_context::mode::opengl: {
                gladLoadGL();
                break;
            }

            case graphics::gl_context::mode::opengl_es: {
#if EKA2L1_PLATFORM(ANDROID)
                if (!gladLoadGLES2Loader((GLADloadproc) eglGetProcAddress)) {
                    LOG_CRITICAL(DRIVER_GRAPHICS, "gladLoadGLES2Loader() failed");
                    return;
                }
#endif

                break;
            }

            default:
                return;
        }

        glad_set_post_callback(gl_post_callback_for_error);
    }

    ogl_graphics_driver::ogl_graphics_driver(const window_system_info &info)
        : shared_graphics_driver(graphic_api::opengl)
        , should_stop(false)
        , surface_update_needed(false)
        , new_surface(nullptr)
        , is_gles(false)
        , support_line_width_(true)
        , point_size(1.0)
        , line_style(pen_style_none)
        , active_input_descriptors_(nullptr)
        , index_buffer_current_(0)
        , feature_flags_(0) {
        context_ = graphics::make_gl_context(info, false, true);

        if (!context_) {
            LOG_ERROR(DRIVER_GRAPHICS, "OGL context failed to create!");
            return;
        }

        init_gl_graphics_library(context_->gl_mode());
        list_queue.max_pending_count_ = 128;

        context_->set_swap_interval(1);

        is_gles = (context_->gl_mode() == graphics::gl_context::mode::opengl_es);

        GLint major_gl = 0;
        GLint minor_gl = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major_gl);
        glGetIntegerv(GL_MINOR_VERSION, &minor_gl);

        if (!is_gles && (major_gl < 4)) {
            std::vector<std::string> GL_REQUIRED_EXTENSIONS = {
                "GL_ARB_draw_elements_base_vertex"
            };

            std::int32_t ext_count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);

            for (std::int32_t i = 0; i < ext_count; i++) {
                const GLubyte *next_extension = glGetStringi(GL_EXTENSIONS, i);
                auto ite = std::find(GL_REQUIRED_EXTENSIONS.begin(), GL_REQUIRED_EXTENSIONS.end(),
                    std::string(reinterpret_cast<const char *>(next_extension)));

                if (ite != GL_REQUIRED_EXTENSIONS.end()) {
                    GL_REQUIRED_EXTENSIONS.erase(ite);
                }
            }

            if (!GL_REQUIRED_EXTENSIONS.empty()) {
                LOG_ERROR(DRIVER_GRAPHICS, "Some OpenGL extensions are missing in order for the emulator to work.");
                LOG_ERROR(DRIVER_GRAPHICS, "Please upgrade your machine! Here is the list of missing extensions.");

                for (const auto &ext_left : GL_REQUIRED_EXTENSIONS) {
                    LOG_ERROR(DRIVER_GRAPHICS, "- {}", ext_left);
                }
            }
        } else {
            if ((minor_gl >= 3) || is_gles) {
                feature_flags_ |= OGL_FEATURE_SUPPORT_ETC2;
            }
        }

        std::int32_t ext_count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);

        for (std::int32_t i = 0; i < ext_count; i++) {
            const GLubyte *next_extension = glGetStringi(GL_EXTENSIONS, i);
            if (strcmp(reinterpret_cast<const char*>(next_extension), "GL_ARB_ES3_compatibility") == 0) {
                feature_flags_ |= OGL_FEATURE_SUPPORT_ETC2;
                break;
            }

            if (strcmp(reinterpret_cast<const char*>(next_extension), "GL_IMG_texture_compression_pvrtc") == 0) {
                feature_flags_ |= OGL_FEATURE_SUPPORT_PVRTC;
            }
        }

        std::string feature = "";

        if (feature_flags_ & OGL_FEATURE_SUPPORT_ETC2) {
            feature += "ETC2Dec;";
        }

        if (feature_flags_ & OGL_FEATURE_SUPPORT_PVRTC) {
            feature += "PVRTDec;";
        }

        if (!feature.empty()) {
            feature.pop_back();
        }

        LOG_TRACE(DRIVER_GRAPHICS, "Supported features (for optimization): {}", feature);
    }

    static constexpr const char *sprite_norm_v_path = "resources//sprite_norm.vert";
    static constexpr const char *sprite_norm_f_path = "resources//sprite_norm.frag";
    static constexpr const char *sprite_mask_f_path = "resources//sprite_mask.frag";
    static constexpr const char *brush_v_path = "resources//brush.vert";
    static constexpr const char *brush_f_path = "resources//brush.frag";
    static constexpr const char *pen_v_path = "resources//pen.vert";
    static constexpr const char *pen_f_path = "resources//pen.frag";

    void ogl_graphics_driver::do_init() {
        auto sprite_norm_vertex_module = std::make_unique<ogl_shader_module>(sprite_norm_v_path, shader_module_type::vertex);        
        auto brush_vertex_module = std::make_unique<ogl_shader_module>(brush_v_path, shader_module_type::vertex);
        auto pen_vertex_module = std::make_unique<ogl_shader_module>(pen_v_path, shader_module_type::vertex);

        auto sprite_norm_fragment_module = std::make_unique<ogl_shader_module>(sprite_norm_f_path, shader_module_type::fragment);
        auto sprite_mask_fragment_module = std::make_unique<ogl_shader_module>(sprite_mask_f_path, shader_module_type::fragment);
        auto brush_fragment_module = std::make_unique<ogl_shader_module>(brush_f_path, shader_module_type::fragment);
        auto pen_fragment_module = std::make_unique<ogl_shader_module>(pen_f_path, shader_module_type::fragment);

        sprite_program = std::make_unique<ogl_shader_program>();
        mask_program = std::make_unique<ogl_shader_program>();
        brush_program = std::make_unique<ogl_shader_program>();
        pen_program = std::make_unique<ogl_shader_program>();

        sprite_program->create(this, sprite_norm_vertex_module.get(), sprite_norm_fragment_module.get());
        mask_program->create(this, sprite_norm_vertex_module.get(), sprite_mask_fragment_module.get());
        brush_program->create(this, brush_vertex_module.get(), brush_fragment_module.get());
        pen_program->create(this, pen_vertex_module.get(), pen_fragment_module.get());

        for (int i = 0; i < GL_BACKEND_MAX_VBO_SLOTS; i++) {
            vbo_slots_[i] = 0;
        }

        static GLushort indices[] = {
            0, 1, 2,
            0, 3, 1
        };

        // Make sprite VAO and VBO
        glGenVertexArrays(1, &sprite_vao);
        glGenBuffers(1, &sprite_vbo);
        glBindVertexArray(sprite_vao);
        glBindBuffer(GL_ARRAY_BUFFER, sprite_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid *)(2 * sizeof(GLfloat)));
        glBindVertexArray(0);

        // Make fill VAO and VBO
        glGenVertexArrays(1, &brush_vao);
        glGenBuffers(1, &brush_vbo);
        glBindVertexArray(brush_vao);
        glBindBuffer(GL_ARRAY_BUFFER, brush_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (GLvoid *)0);
        glBindVertexArray(0);

        // Make pen VAO and VBO
        glGenVertexArrays(1, &pen_vao);
        glGenBuffers(1, &pen_vbo);
        glBindVertexArray(pen_vao);
        glBindBuffer(GL_ARRAY_BUFFER, pen_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (GLvoid *)0);
        glBindVertexArray(0);

        glGenBuffers(1, &sprite_ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sprite_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        glGenBuffers(1, &pen_ibo);

        color_loc = sprite_program->get_uniform_location("u_color").value_or(-1);
        proj_loc = sprite_program->get_uniform_location("u_proj").value_or(-1);
        model_loc = sprite_program->get_uniform_location("u_model").value_or(-1);
        flip_loc = sprite_program->get_uniform_location("u_flip").value_or(-1);
        in_position_loc = sprite_program->get_attrib_location("in_position").value_or(-1);
        in_texcoord_loc = sprite_program->get_attrib_location("in_texcoord").value_or(-1);

        color_loc_brush = brush_program->get_uniform_location("u_color").value_or(-1);
        proj_loc_brush = brush_program->get_uniform_location("u_proj").value_or(-1);
        model_loc_brush = brush_program->get_uniform_location("u_model").value_or(-1);

        color_loc_mask = mask_program->get_uniform_location("u_color").value_or(-1);
        proj_loc_mask = mask_program->get_uniform_location("u_proj").value_or(-1);
        model_loc_mask = mask_program->get_uniform_location("u_model").value_or(-1);
        invert_loc_mask = mask_program->get_uniform_location("u_invert").value_or(-1);
        source_loc_mask = mask_program->get_uniform_location("u_tex").value_or(-1);
        mask_loc_mask = mask_program->get_uniform_location("u_mask").value_or(-1);
        flip_loc_mask = mask_program->get_uniform_location("u_flip").value_or(-1);
        flat_blend_loc_mask = mask_program->get_uniform_location("u_flat").value_or(-1);
        in_position_loc_mask = mask_program->get_attrib_location("in_position").value_or(-1);
        in_texcoord_loc_mask = mask_program->get_attrib_location("in_texcoord").value_or(-1);
        
        color_loc_pen = pen_program->get_uniform_location("u_color").value_or(-1);
        proj_loc_pen = pen_program->get_uniform_location("u_proj").value_or(-1);
        model_loc_pen = pen_program->get_uniform_location("u_model").value_or(-1);
        point_size_loc_pen = pen_program->get_uniform_location("u_pointSize").value_or(-1);
        pattern_bytes_loc_pen = pen_program->get_uniform_location("u_pattern").value_or(-1);
        viewport_loc_pen = pen_program->get_uniform_location("u_viewport").value_or(-1);
    }

    void ogl_graphics_driver::bind_swapchain_framebuf() {
        if (surface_update_needed) {
            context_->update_surface(new_surface);
            surface_update_needed = false;
        }

        context_->update();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ogl_graphics_driver::update_surface(void *new_surface_set) {
        new_surface = new_surface_set;
        surface_update_needed = true;
    }

    void ogl_graphics_driver::draw_rectangle(command_helper &helper) {
        if (!brush_program) {
            do_init();
        }

        eka2l1::rect brush_rect;
        helper.pop(brush_rect);

        brush_program->use(this);

        // Build model matrix
        glm::mat4 model_matrix = glm::identity<glm::mat4>();
        model_matrix = glm::translate(model_matrix, { brush_rect.top.x, brush_rect.top.y, 0.0f });
        model_matrix = glm::scale(model_matrix, { brush_rect.size.x, brush_rect.size.y, 0.0f });

        glUniformMatrix4fv(model_loc_brush, 1, false, glm::value_ptr(model_matrix));
        glUniformMatrix4fv(proj_loc_brush, 1, false, glm::value_ptr(projection_matrix));

        // Supply brush
        glUniform4fv(color_loc_brush, 1, brush_color.elements.data());

        static GLfloat brush_verts_default[] = {
            0.0f,
            1.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            1.0f,
        };

        glBindVertexArray(brush_vao);
        glBindBuffer(GL_ARRAY_BUFFER, brush_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(brush_verts_default), nullptr, GL_STATIC_DRAW);
        glBufferData(GL_ARRAY_BUFFER, sizeof(brush_verts_default), brush_verts_default, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (GLvoid *)0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sprite_ibo);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        glBindVertexArray(0);
    }

    void ogl_graphics_driver::draw_bitmap(command_helper &helper) {
        if (!sprite_program) {
            do_init();
        }

        // Get bitmap to draw
        drivers::handle to_draw = 0;
        helper.pop(to_draw);

        bitmap *bmp = get_bitmap(to_draw);
        texture *draw_texture = nullptr;

        if (!bmp) {
            draw_texture = reinterpret_cast<texture*>(get_graphics_object(to_draw));

            if (!draw_texture) {
                LOG_ERROR(DRIVER_GRAPHICS, "Invalid bitmap handle to draw");
                return;
            }
        } else {
            draw_texture = bmp->tex.get();
        }

        drivers::handle mask_to_use = 0;
        helper.pop(mask_to_use);

        bitmap *mask_bmp = nullptr;
        texture *mask_draw_texture = nullptr;

        if (mask_to_use) {
            mask_bmp = get_bitmap(mask_to_use);

            if (!mask_bmp) {
                mask_draw_texture = reinterpret_cast<texture*>(get_graphics_object(mask_to_use));

                if (!mask_draw_texture) {
                    LOG_ERROR(DRIVER_GRAPHICS, "Mask handle was provided but invalid!");
                    return;
                }
            } else {
                mask_draw_texture = mask_bmp->tex.get();
            }
        }

        eka2l1::rect dest_rect;
        helper.pop(dest_rect);

        if (mask_bmp) {
            mask_program->use(this);
        } else {
            sprite_program->use(this);
        }

        // Build texcoords
        eka2l1::rect source_rect;
        helper.pop(source_rect);

        eka2l1::vec2 origin = eka2l1::vec2(0, 0);
        helper.pop(origin);

        float rotation = 0.0f;
        helper.pop(rotation);

        struct sprite_vertex {
            float top[2];
            float coord[2];
        } verts[4];

        static GLfloat verts_default[] = {
            0.0f,
            1.0f,
            0.0f,
            1.0f,
            1.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
        };

        void *vert_pointer = verts_default;

        if (!source_rect.empty()) {
            const float texel_width = 1.0f / draw_texture->get_size().x;
            const float texel_height = 1.0f / draw_texture->get_size().y;

            // Bottom left
            verts[0].top[0] = 0.0f;
            verts[0].top[1] = 1.0f;

            verts[0].coord[0] = source_rect.top.x * texel_width;
            verts[0].coord[1] = (source_rect.top.y + source_rect.size.y) * texel_height;

            // Top right
            verts[1].top[0] = 1.0f;
            verts[1].top[1] = 0.0f;

            verts[1].coord[0] = (source_rect.top.x + source_rect.size.x) * texel_width;
            verts[1].coord[1] = source_rect.top.y * texel_height;

            // Top left
            verts[2].top[0] = 0.0f;
            verts[2].top[1] = 0.0f;

            verts[2].coord[0] = source_rect.top.x * texel_width;
            verts[2].coord[1] = source_rect.top.y * texel_height;

            // Bottom right
            verts[3].top[0] = 1.0f;
            verts[3].top[1] = 1.0f;

            verts[3].coord[0] = (source_rect.top.x + source_rect.size.x) * texel_width;
            verts[3].coord[1] = (source_rect.top.y + source_rect.size.y) * texel_height;

            vert_pointer = verts;
        }

        glBindVertexArray(sprite_vao);
        glBindBuffer(GL_ARRAY_BUFFER, sprite_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_STATIC_DRAW);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), vert_pointer, GL_STATIC_DRAW);
        glEnableVertexAttribArray(mask_draw_texture ? in_position_loc_mask : in_position_loc);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid *)0);
        glEnableVertexAttribArray(mask_draw_texture ? in_texcoord_loc_mask : in_texcoord_loc);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid *)(2 * sizeof(GLfloat)));

        if (mask_draw_texture) {
            glUniform1i(source_loc_mask, 0);
            glUniform1i(mask_loc_mask, 1);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(draw_texture->texture_handle()));

        if (mask_draw_texture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(mask_draw_texture->texture_handle()));
        }

        // Build model matrix
        glm::mat4 model_matrix = glm::identity<glm::mat4>();
        model_matrix = glm::translate(model_matrix, { dest_rect.top.x, dest_rect.top.y, 0.0f });

        if (source_rect.size.x == 0) {
            source_rect.size.x = draw_texture->get_size().x;
        }

        if (source_rect.size.y == 0) {
            source_rect.size.y = draw_texture->get_size().y;
        }

        if (dest_rect.size.x == 0) {
            dest_rect.size.x = source_rect.size.x;
        }

        if (dest_rect.size.y == 0) {
            dest_rect.size.y = source_rect.size.y;
        }

        model_matrix = glm::translate(model_matrix, glm::vec3(static_cast<float>(origin.x), static_cast<float>(origin.y), 0.0f));
        model_matrix = glm::rotate(model_matrix, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
        model_matrix = glm::translate(model_matrix, glm::vec3(static_cast<float>(-origin.x), static_cast<float>(-origin.y), 0.0f));

        model_matrix = glm::scale(model_matrix, glm::vec3(dest_rect.size.x, dest_rect.size.y, 1.0f));

        glUniformMatrix4fv((mask_draw_texture ? model_loc_mask : model_loc), 1, false, glm::value_ptr(model_matrix));
        glUniformMatrix4fv((mask_draw_texture ? proj_loc_mask : proj_loc), 1, false, glm::value_ptr(projection_matrix));

        // Supply brush
        std::uint32_t flags = 0;
        helper.pop(flags);

        const GLfloat color[] = { 255.0f, 255.0f, 255.0f, 255.0f };

        if (flags & bitmap_draw_flag_use_brush) {
            glUniform4fv((mask_draw_texture ? color_loc_mask : color_loc), 1, brush_color.elements.data());
        } else {
            glUniform4fv((mask_draw_texture ? color_loc_mask : color_loc), 1, color);
        }

        glUniform1f((mask_draw_texture ? flip_loc_mask : flip_loc), (flags & bitmap_draw_flag_no_flip) ? 1.0f : -1.0f);

        if (mask_draw_texture) {
            glUniform1f(invert_loc_mask, (flags & bitmap_draw_flag_invert_mask) ? 1.0f : 0.0f);
            glUniform1f(flat_blend_loc_mask, (flags & bitmap_draw_flag_flat_blending) ? 1.0f : 0.0f);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sprite_ibo);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        glBindVertexArray(0);
    }

    void ogl_graphics_driver::clip_rect(command_helper &helper) {
        eka2l1::rect clip_rect;
        helper.pop(clip_rect);

        glScissor(clip_rect.top.x, (clip_rect.size.y > 0) ? (current_fb_height - (clip_rect.top.y - clip_rect.size.y)) : clip_rect.top.y, clip_rect.size.x, common::abs(clip_rect.size.y));
    }

    static GLenum prim_mode_to_gl_enum(const graphics_primitive_mode prim_mode) {
        switch (prim_mode) {
        case graphics_primitive_mode::triangles:
            return GL_TRIANGLES;
        
        case graphics_primitive_mode::line_loop:
            return GL_LINE_LOOP;

        case graphics_primitive_mode::line_strip:
            return GL_LINE_STRIP;

        case graphics_primitive_mode::lines:
            return GL_LINES;

        case graphics_primitive_mode::points:
            return GL_POINTS;

        case graphics_primitive_mode::triangle_fan:
            return GL_TRIANGLE_FAN;

        case graphics_primitive_mode::triangle_strip:
            return GL_TRIANGLE_STRIP;

        default:
            break;
        }

        return GL_INVALID_ENUM;
    }

    void ogl_graphics_driver::draw_indexed(command_helper &helper) {
        graphics_primitive_mode prim_mode = graphics_primitive_mode::triangles;
        int count = 0;
        data_format val_type = data_format::word;
        int index_off = 0;
        int vert_off = 0;

        helper.pop(prim_mode);
        helper.pop(count);
        helper.pop(val_type);
        helper.pop(index_off);
        helper.pop(vert_off);

        std::uint64_t index_off_64 = index_off;

        if (active_input_descriptors_) {
            active_input_descriptors_->activate(vbo_slots_);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_current_);

        if (vert_off == 0) {
            glDrawElements(prim_mode_to_gl_enum(prim_mode), count, data_format_to_gl_enum(val_type), reinterpret_cast<GLvoid *>(index_off_64));
        } else {
            glDrawElementsBaseVertex(prim_mode_to_gl_enum(prim_mode), count, data_format_to_gl_enum(val_type), reinterpret_cast<GLvoid *>(index_off_64), vert_off);
        }
    }

    void ogl_graphics_driver::draw_array(command_helper &helper) {
        graphics_primitive_mode prim_mode = graphics_primitive_mode::triangles;
        std::int32_t first = 0;
        std::int32_t count = 0;
        std::int32_t instance_count = 0;

        helper.pop(prim_mode);
        helper.pop(first);
        helper.pop(count);
        helper.pop(instance_count);

        if (active_input_descriptors_) {
            active_input_descriptors_->activate(vbo_slots_);
        }

        if (instance_count == 0) {
            glDrawArrays(prim_mode_to_gl_enum(prim_mode), first, count);
        } else {
            glDrawArraysInstanced(prim_mode_to_gl_enum(prim_mode), first, count, instance_count);
        }
    }

    void ogl_graphics_driver::set_viewport(const eka2l1::rect &viewport) {
        glViewport(viewport.top.x, current_fb_height - (viewport.top.y + viewport.size.y), viewport.size.x, viewport.size.y);
    }

    void ogl_graphics_driver::set_feature(command_helper &helper) {
        drivers::graphics_feature feature;
        bool enable = true;

        helper.pop(feature);
        helper.pop(enable);

        GLenum translated_feature;
        switch (feature) {
        case drivers::graphics_feature::blend:
            translated_feature = GL_BLEND;
            break;

        case drivers::graphics_feature::clipping:
            translated_feature = GL_SCISSOR_TEST;
            break;

        case drivers::graphics_feature::cull:
            translated_feature = GL_CULL_FACE;
            break;

        case drivers::graphics_feature::depth_test:
            translated_feature = GL_DEPTH_TEST;
            break;

        case drivers::graphics_feature::stencil_test:
            translated_feature = GL_STENCIL_TEST;
            break;

        case drivers::graphics_feature::sample_coverage:
            translated_feature = GL_SAMPLE_COVERAGE;
            break;

        case drivers::graphics_feature::sample_alpha_to_one:
            translated_feature = GL_SAMPLE_ALPHA_TO_ONE;
            break;

        case drivers::graphics_feature::sample_alpha_to_coverage:
            translated_feature = GL_SAMPLE_ALPHA_TO_COVERAGE;
            break;

        case drivers::graphics_feature::polygon_offset_fill:
            translated_feature = GL_POLYGON_OFFSET_FILL;
            break;

        case drivers::graphics_feature::line_smooth:
            translated_feature = GL_LINE_SMOOTH;
            break;

        case drivers::graphics_feature::multisample:
            translated_feature = GL_MULTISAMPLE;
            break;

        case drivers::graphics_feature::dither:
            translated_feature = GL_DITHER;
            break;

        default:
            return;
        }

        if (enable) {
            glEnable(translated_feature);
        } else {
            glDisable(translated_feature);
        }
    }

    void ogl_graphics_driver::set_viewport(command_helper &helper) {
        eka2l1::rect viewport;
        helper.pop(viewport);

        set_viewport(viewport);
    }

    static GLenum blend_equation_to_gl_enum(const blend_equation eq) {
        switch (eq) {
        case blend_equation::add:
            return GL_FUNC_ADD;

        case blend_equation::sub:
            return GL_FUNC_SUBTRACT;

        case blend_equation::isub:
            return GL_FUNC_REVERSE_SUBTRACT;

        default:
            break;
        }

        return GL_INVALID_ENUM;
    }

    static GLenum blend_factor_to_gl_enum(const blend_factor fac) {
        switch (fac) {
        case blend_factor::one:
            return GL_ONE;

        case blend_factor::zero:
            return GL_ZERO;

        case blend_factor::frag_out_alpha:
            return GL_SRC_ALPHA;

        case blend_factor::one_minus_frag_out_alpha:
            return GL_ONE_MINUS_SRC_ALPHA;

        case blend_factor::current_alpha:
            return GL_DST_ALPHA;

        case blend_factor::one_minus_current_alpha:
            return GL_ONE_MINUS_DST_ALPHA;

        case blend_factor::frag_out_color:
            return GL_SRC_COLOR;

        case blend_factor::one_minus_frag_out_color:
            return GL_ONE_MINUS_SRC_COLOR;

        case blend_factor::current_color:
            return GL_DST_COLOR;

        case blend_factor::one_minus_current_color:
            return GL_ONE_MINUS_DST_COLOR;

        case blend_factor::frag_out_alpha_saturate:
            return GL_SRC_ALPHA_SATURATE;

        default:
            break;
        }

        return GL_INVALID_ENUM;
    }

    void ogl_graphics_driver::blend_formula(command_helper &helper) {
        blend_equation rgb_equation = blend_equation::add;
        blend_equation a_equation = blend_equation::add;
        blend_factor rgb_frag_out_factor = blend_factor::one;
        blend_factor rgb_current_factor = blend_factor::zero;
        blend_factor a_frag_out_factor = blend_factor::one;
        blend_factor a_current_factor = blend_factor::zero;

        helper.pop(rgb_equation);
        helper.pop(a_equation);
        helper.pop(rgb_frag_out_factor);
        helper.pop(rgb_current_factor);
        helper.pop(a_frag_out_factor);
        helper.pop(a_current_factor);

        glBlendEquationSeparate(blend_equation_to_gl_enum(rgb_equation), blend_equation_to_gl_enum(a_equation));
        glBlendFuncSeparate(blend_factor_to_gl_enum(rgb_frag_out_factor), blend_factor_to_gl_enum(rgb_current_factor),
            blend_factor_to_gl_enum(a_frag_out_factor), blend_factor_to_gl_enum(a_current_factor));
    }

    static const GLint rendering_face_to_gl_enum(const rendering_face face) {
        switch (face) {
        case rendering_face::back:
            return GL_BACK;

        case rendering_face::front:
            return GL_FRONT;

        case rendering_face::back_and_front:
            return GL_FRONT_AND_BACK;

        default:
            break;
        }

        return GL_FRONT_AND_BACK;
    }

    static const GLint stencil_action_to_gl_enum(const stencil_action action) {
        switch (action) {
        case stencil_action::keep:
            return GL_KEEP;

        case stencil_action::replace:
            return GL_REPLACE;

        case stencil_action::set_to_zero:
            return GL_ZERO;

        case stencil_action::invert:
            return GL_INVERT;

        case stencil_action::decrement:
            return GL_DECR;

        case stencil_action::decrement_wrap:
            return GL_DECR_WRAP;

        case stencil_action::increment:
            return GL_INCR;

        case stencil_action::increment_wrap:
            return GL_INCR_WRAP;

        default:
            break;
        }

        return GL_KEEP;
    }

    void ogl_graphics_driver::set_stencil_action(command_helper &helper) {
        rendering_face face_to_operate = rendering_face::back_and_front;
        stencil_action on_stencil_fail = stencil_action::keep;
        stencil_action on_stencil_pass_depth_fail = stencil_action::keep;
        stencil_action on_stencil_depth_pass = stencil_action::replace;

        helper.pop(face_to_operate);
        helper.pop(on_stencil_fail);
        helper.pop(on_stencil_pass_depth_fail);
        helper.pop(on_stencil_depth_pass);

        glStencilOpSeparate(rendering_face_to_gl_enum(face_to_operate), stencil_action_to_gl_enum(on_stencil_fail),
            stencil_action_to_gl_enum(on_stencil_pass_depth_fail), stencil_action_to_gl_enum(on_stencil_depth_pass));
    }

    static const GLint condition_func_to_gl_enum(const condition_func func) {
        switch (func) {
        case condition_func::never:
            return GL_NEVER;

        case condition_func::always:
            return GL_ALWAYS;

        case condition_func::equal:
            return GL_EQUAL;

        case condition_func::not_equal:
            return GL_NOTEQUAL;

        case condition_func::less:
            return GL_LESS;

        case condition_func::less_or_equal:
            return GL_LEQUAL;

        case condition_func::greater:
            return GL_GREATER;

        case condition_func::greater_or_equal:
            return GL_GEQUAL;

        default:
            break;
        }

        return GL_NEVER;
    }

    void ogl_graphics_driver::set_stencil_pass_condition(command_helper &helper) {
        condition_func pass_func = condition_func::always;
        rendering_face face_to_operate = rendering_face::back_and_front;
        std::int32_t ref_value = 0;
        std::uint32_t mask = 0xFF;

        helper.pop(face_to_operate);
        helper.pop(pass_func);
        helper.pop(ref_value);
        helper.pop(mask);

        glStencilFuncSeparate(rendering_face_to_gl_enum(face_to_operate), condition_func_to_gl_enum(pass_func),
            ref_value, mask);
    }

    void ogl_graphics_driver::set_stencil_mask(command_helper &helper) {
        rendering_face face_to_operate = rendering_face::back_and_front;
        std::uint32_t mask = 0xFF;

        helper.pop(face_to_operate);
        helper.pop(mask);

        glStencilMaskSeparate(rendering_face_to_gl_enum(face_to_operate), mask);
    }

    void ogl_graphics_driver::set_depth_mask(command_helper &helper) {
        std::uint32_t mask = 0xFF;
        helper.pop(mask);

        glDepthMask(mask);
    }

    void ogl_graphics_driver::clear(command_helper &helper) {
        float color_to_clear[6];
        std::uint8_t clear_bits = 0;

        helper.pop(color_to_clear[0]);
        helper.pop(color_to_clear[1]);
        helper.pop(color_to_clear[2]);
        helper.pop(color_to_clear[3]);
        helper.pop(color_to_clear[4]);
        helper.pop(color_to_clear[5]);
        helper.pop(clear_bits);

        std::uint32_t gl_flags = 0;

        if (clear_bits & draw_buffer_bit_color_buffer) {
            glClearColor(color_to_clear[0], color_to_clear[1], color_to_clear[2], color_to_clear[3]);
            gl_flags |= GL_COLOR_BUFFER_BIT;
        }

        if (clear_bits & draw_buffer_bit_depth_buffer) {
#ifdef EKA2L1_PLATFORM_ANDROID
            glClearDepthf(color_to_clear[4]);
#else
            glClearDepth(color_to_clear[4]);
#endif
            gl_flags |= GL_DEPTH_BUFFER_BIT;
        }

        if (clear_bits & draw_buffer_bit_stencil_buffer) {
            glClearStencil(static_cast<GLint>(color_to_clear[5] * 255.0f));
            gl_flags |= GL_STENCIL_BUFFER_BIT;
        }

        glClear(gl_flags);
    }

    void ogl_graphics_driver::set_point_size(command_helper &helper) {
        std::uint8_t to_set_point_size = 0;
        helper.pop(to_set_point_size);

        point_size = static_cast<float>(to_set_point_size);
    }

    void ogl_graphics_driver::set_pen_style(command_helper &helper) {
        pen_style to_set_style = pen_style_none;
        helper.pop(to_set_style);

        line_style = to_set_style;
    }

    void ogl_graphics_driver::prepare_draw_lines_shared() {
        std::uint32_t bit_pattern = 0;

        // Taken from techwinder's code in the following link below. Thank you!
        // https://stackoverflow.com/questions/6017176/gllinestipple-deprecated-in-opengl-3-1
        switch (line_style) {
        case pen_style_solid:
            bit_pattern = 0xFFFF;
            break;

        case pen_style_dotted:
            bit_pattern = 0x6666;
            break;

        case pen_style_dashed:
            bit_pattern = 0x3F3F;
            break;
        
        case pen_style_dashed_dot:
            bit_pattern = 0xFF18;
            break;

        case pen_style_dashed_dot_dot:
            bit_pattern = 0x7E66;
            break;

        default:
            LOG_WARN(DRIVER_GRAPHICS, "Unrecognised pen style {}!", static_cast<int>(line_style));
            return;
        }

        pen_program->use(this);

        glUniform1f(point_size_loc_pen, point_size);
        glUniform1ui(pattern_bytes_loc_pen, bit_pattern);
        
        glm::mat4 model_matrix = glm::identity<glm::mat4>();

        glUniformMatrix4fv(model_loc_pen, 1, false, glm::value_ptr(model_matrix));
        glUniformMatrix4fv(proj_loc_pen, 1, false, glm::value_ptr(projection_matrix));
        glUniform4fv(color_loc_pen, 1, brush_color.elements.data());

        GLfloat viewport[2] = { static_cast<GLfloat>(current_fb_width), static_cast<GLfloat>(current_fb_height) };
        glUniform2fv(viewport_loc_pen, 1, viewport);
    }

    void ogl_graphics_driver::draw_line(command_helper &helper) {
        if (line_style == pen_style_none) {
            return;
        }

        eka2l1::point start;
        eka2l1::point end;

        helper.pop(start);
        helper.pop(end);

        prepare_draw_lines_shared();

        GLfloat data_send_array[] = {
            static_cast<float>(start.x), static_cast<float>(start.y),
            static_cast<float>(end.x), static_cast<float>(end.y)
        };

        glBindVertexArray(pen_vao);
        glBindBuffer(GL_ARRAY_BUFFER, pen_vbo);
        glEnableVertexAttribArray(0);
        glBufferData(GL_ARRAY_BUFFER, sizeof(data_send_array), nullptr, GL_STATIC_DRAW);
        glBufferData(GL_ARRAY_BUFFER, sizeof(data_send_array), data_send_array, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (GLvoid *)0);

        glDrawArrays(GL_LINES, 0, 2);
    }

    void ogl_graphics_driver::draw_polygon(command_helper &helper) {
        if (line_style == pen_style_none) {
            return;
        }
        
        prepare_draw_lines_shared();
        
        std::size_t point_count = 0;
        eka2l1::point *point_list = nullptr;

        helper.pop(point_count);
        helper.pop(point_list);

        prepare_draw_lines_shared();

        glBindVertexArray(pen_vao);
        glBindBuffer(GL_ARRAY_BUFFER, pen_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_INT, GL_FALSE, 8, (GLvoid *)0);
        glBufferData(GL_ARRAY_BUFFER, point_count * 8, nullptr, GL_STATIC_DRAW);
        glBufferData(GL_ARRAY_BUFFER, point_count * 8, point_list, GL_STATIC_DRAW);

        // Generate indices for force primitive restart
        // Don't try to recreate buffer (note that nullptr upper is a method of fast clearing)
        std::vector<std::uint32_t> indicies((point_count - 1) * 2);
        for (std::size_t i = 0; i < point_count - 1; i++) {
            indicies[i * 2] = static_cast<std::uint32_t>(i);
            indicies[i * 2 + 1] = static_cast<std::uint32_t>(i + 1);
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pen_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indicies.size() * sizeof(int), indicies.data(), GL_STATIC_DRAW);

        glDrawElements(GL_LINES, static_cast<GLsizei>(indicies.size()), GL_UNSIGNED_INT, 0);
    }

    void ogl_graphics_driver::set_cull_face(command_helper &helper) {
        rendering_face face_to_cull = rendering_face::back;
        helper.pop(face_to_cull);

        switch (face_to_cull) {
        case rendering_face::back:
            glCullFace(GL_BACK);
            break;

        case rendering_face::front:
            glCullFace(GL_FRONT);
            break;

        case rendering_face::back_and_front:
            glCullFace(GL_FRONT_AND_BACK);
            break;

        default:
            break;
        }
    }

    void ogl_graphics_driver::set_depth_func(command_helper &helper) {
        condition_func func = condition_func::always;
        helper.pop(func);

        glDepthFunc(condition_func_to_gl_enum(func));
    }

    void ogl_graphics_driver::set_front_face_rule(command_helper &helper) {
        rendering_face_determine_rule rule = rendering_face_determine_rule::vertices_counter_clockwise;
        helper.pop(rule);

        if (rule == rendering_face_determine_rule::vertices_counter_clockwise) {
            glFrontFace(GL_CCW);
        } else {
            glFrontFace(GL_CW);
        }
    }

    void ogl_graphics_driver::set_color_mask(command_helper &helper) {
        std::uint8_t dat = 0;
        helper.pop(dat);

        glColorMask(dat & 1, dat & 2, dat & 4, dat & 8);
    }

    void ogl_graphics_driver::set_uniform(command_helper &helper) {
        drivers::shader_set_var_type var_type;
        std::uint8_t *data = nullptr;
        int binding = 0;

        helper.pop(var_type);
        helper.pop(data);
        helper.pop(binding);

        switch (var_type) {
        case shader_set_var_type::integer: {
            glUniform1i(binding, *reinterpret_cast<const GLint *>(data));
            delete data;

            return;
        }

        case shader_set_var_type::real:
            glUniform1f(binding, *reinterpret_cast<const GLfloat*>(data));
            delete data;

            return;

        case shader_set_var_type::mat4: {
            glUniformMatrix4fv(binding, 1, GL_FALSE, reinterpret_cast<const GLfloat *>(data));
            delete data;

            return;
        }

        case shader_set_var_type::vec3: {
            glUniform3fv(binding, 1, reinterpret_cast<const GLfloat *>(data));
            delete data;

            return;
        }

        case shader_set_var_type::vec4: {
            glUniform4fv(binding, 1, reinterpret_cast<const GLfloat *>(data));
            delete data;

            return;
        }

        default:
            break;
        }

        LOG_ERROR(DRIVER_GRAPHICS, "Unable to set GL uniform!");
    }

    void ogl_graphics_driver::set_texture_for_shader(command_helper &helper) {
        std::int32_t texture_slot = 0;
        std::int32_t shader_binding = 0;
        drivers::shader_module_type mtype = drivers::shader_module_type::fragment;

        helper.pop(texture_slot);
        helper.pop(shader_binding);
        helper.pop(mtype);

        glUniform1i(shader_binding, texture_slot);
    }

    void ogl_graphics_driver::bind_vertex_buffers(command_helper &helper) {
        drivers::handle *arr = 0;
        std::uint32_t starting_slots = 0;
        std::uint32_t count = 0;

        helper.pop(arr);
        helper.pop(starting_slots);
        helper.pop(count);

        if (starting_slots + count >= GL_BACKEND_MAX_VBO_SLOTS) {
            LOG_ERROR(DRIVER_GRAPHICS, "Slot to bind VBO exceed maximum (startSlot={}, count={})", starting_slots, count);
            delete arr;

            return;
        }

        for (std::uint32_t i = 0; i < count; i++) {
            ogl_buffer *bufobj = reinterpret_cast<ogl_buffer *>(get_graphics_object(arr[i]));

            if (!bufobj) {
                continue;
            }

            vbo_slots_[starting_slots + i] = bufobj->buffer_handle();
        }

        delete arr;
    }

    void ogl_graphics_driver::bind_index_buffer(command_helper &helper) {
        drivers::handle h = 0;
        helper.pop(h);

        ogl_buffer *bufobj = reinterpret_cast<ogl_buffer *>(get_graphics_object(h));
        if (!bufobj) {
            return;
        }

        index_buffer_current_ = bufobj->buffer_handle();
    }

    void ogl_graphics_driver::bind_input_descriptors(command_helper &helper) {
        drivers::handle h = 0;
        helper.pop(h);

        input_descriptors_ogl *descs = reinterpret_cast<input_descriptors_ogl *>(get_graphics_object(h));
        if (!descs) {
            return;
        }

        active_input_descriptors_ = descs;
    }

    void ogl_graphics_driver::set_line_width(command_helper &helper) {
        if (!support_line_width_) {
            return;
        }

        float w = 0;
        helper.pop(w);

        GLint range[2];
        glGetIntegerv(GL_ALIASED_LINE_WIDTH_RANGE, range);

        if (range[1] >= w) {
            glad_glLineWidth(w);

            if (glGetError() == GL_INVALID_VALUE) {
                LOG_INFO(DRIVER_GRAPHICS, "OpenGL driver does not support line width feature. Disabled from now on.");
                support_line_width_ = false;
            }
        }
    }

    void ogl_graphics_driver::set_depth_bias(command_helper &helper) {
        float constant_factor = 0.0f;
        float clamp = 0.0f;
        float slope_factor = 0.0f;

        helper.pop(constant_factor);
        helper.pop(clamp);
        helper.pop(slope_factor);

        glPolygonOffset(slope_factor, constant_factor);
    }

    void ogl_graphics_driver::save_gl_state() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &backup.last_program);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &backup.last_texture);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &backup.last_active_texture);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &backup.last_array_buffer);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &backup.last_element_array_buffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &backup.last_vertex_array);
        glGetIntegerv(GL_BLEND_SRC_RGB, &backup.last_blend_src_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &backup.last_blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_RGB, &backup.last_blend_dst_rgb);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &backup.last_blend_dst_alpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &backup.last_blend_equation_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &backup.last_blend_equation_alpha);
        glGetIntegerv(GL_VIEWPORT, backup.last_viewport);
        glGetIntegerv(GL_SCISSOR_BOX, backup.last_scissor);
        backup.last_enable_blend = glIsEnabled(GL_BLEND);
        backup.last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
        backup.last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
        backup.last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);
    }

    void ogl_graphics_driver::load_gl_state() {
        glUseProgram(backup.last_program);
        glActiveTexture(static_cast<GLenum>(backup.last_active_texture));
        glBindTexture(GL_TEXTURE_2D, backup.last_texture);
        glBindVertexArray(backup.last_vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, backup.last_array_buffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, backup.last_element_array_buffer);

        glBlendEquationSeparate(static_cast<GLenum>(backup.last_blend_equation_rgb),
            static_cast<GLenum>(backup.last_blend_equation_alpha));

        glBlendFuncSeparate(static_cast<GLenum>(backup.last_blend_src_rgb),
            static_cast<GLenum>(backup.last_blend_dst_rgb),
            static_cast<GLenum>(backup.last_blend_src_alpha),
            static_cast<GLenum>(backup.last_blend_dst_alpha));

        if (backup.last_enable_blend == GL_TRUE) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }

        if (backup.last_enable_cull_face == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }

        if (backup.last_enable_depth_test == GL_TRUE) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }

        if (backup.last_enable_scissor_test == GL_TRUE) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        glViewport(backup.last_viewport[0],
            backup.last_viewport[1],
            static_cast<GLsizei>(backup.last_viewport[2]),
            static_cast<GLsizei>(backup.last_viewport[3]));

        glScissor(backup.last_scissor[0],
            backup.last_scissor[1],
            backup.last_scissor[2],
            backup.last_scissor[3]);
    }

    std::unique_ptr<graphics_command_list> ogl_graphics_driver::new_command_list() {
        return std::make_unique<server_graphics_command_list>();
    }

    std::unique_ptr<graphics_command_list_builder> ogl_graphics_driver::new_command_builder(graphics_command_list *list) {
        return std::make_unique<server_graphics_command_list_builder>(list);
    }

    void ogl_graphics_driver::submit_command_list(graphics_command_list &command_list) {
        list_queue.push(static_cast<server_graphics_command_list &>(command_list));
    }

    void ogl_graphics_driver::display(command_helper &helper) {
        context_->swap_buffers();

        disp_hook_();
        helper.finish(this, 0);
    }

    void ogl_graphics_driver::dispatch(command *cmd) {
        command_helper helper(cmd);

        switch (cmd->opcode_) {
        case graphics_driver_draw_bitmap: {
            draw_bitmap(helper);
            break;
        }

        case graphics_driver_clip_rect: {
            clip_rect(helper);
            break;
        }

        case graphics_driver_draw_indexed: {
            draw_indexed(helper);
            break;
        }

        case graphics_driver_draw_array:
            draw_array(helper);
            break;

        case graphics_driver_backup_state: {
            save_gl_state();
            break;
        }

        case graphics_driver_restore_state: {
            load_gl_state();
            break;
        }

        case graphics_driver_blend_formula: {
            blend_formula(helper);
            break;
        }

        case graphics_driver_set_feature: {
            set_feature(helper);
            break;
        }

        case graphics_driver_stencil_set_action:
            set_stencil_action(helper);
            break;

        case graphics_driver_stencil_pass_condition:
            set_stencil_pass_condition(helper);
            break;

        case graphics_driver_stencil_set_mask:
            set_stencil_mask(helper);
            break;

        case graphics_driver_clear: {
            clear(helper);
            break;
        }

        case graphics_driver_set_viewport: {
            set_viewport(helper);
            break;
        }

        case graphics_driver_display: {
            display(helper);
            break;
        }

        case graphics_driver_draw_rectangle: {
            draw_rectangle(helper);
            break;
        }

        case graphics_driver_draw_line:
            draw_line(helper);
            break;

        case graphics_driver_draw_polygon:
            draw_polygon(helper);
            break;

        case graphics_driver_set_point_size:
            set_point_size(helper);
            break;

        case graphics_driver_set_pen_style:
            set_pen_style(helper);
            break;

        case graphics_driver_cull_face:
            set_cull_face(helper);
            break;

        case graphics_driver_set_front_face_rule:
            set_front_face_rule(helper);
            break;

        case graphics_driver_set_color_mask:
            set_color_mask(helper);
            break;

        case graphics_driver_set_depth_func:
            set_depth_func(helper);
            break;

        case graphics_driver_set_texture_for_shader:
            set_texture_for_shader(helper);
            break;

        case graphics_driver_set_uniform:
            set_uniform(helper);
            break;

        case graphics_driver_depth_set_mask:
            set_depth_mask(helper);
            break;

        case graphics_driver_bind_vertex_buffers:
            bind_vertex_buffers(helper);
            break;

        case graphics_driver_bind_index_buffer:
            bind_index_buffer(helper);
            break;

        case graphics_driver_bind_input_descriptor:
            bind_input_descriptors(helper);
            break;

        case graphics_driver_set_line_width:
            set_line_width(helper);
            break;

        case graphics_driver_set_depth_bias:
            set_depth_bias(helper);
            break;

        default:
            shared_graphics_driver::dispatch(cmd);
            break;
        }
    }

    void ogl_graphics_driver::run() {
        while (!should_stop) {
            std::optional<server_graphics_command_list> list = list_queue.pop();

            if (!list) {
                LOG_ERROR(DRIVER_GRAPHICS, "Corrupted graphics command list! Emulation halt.");
                break;
            }

            command *cmd = list->list_.first_;
            command *next = nullptr;

            while (cmd) {
                dispatch(cmd);
                next = cmd->next_;

                // TODO: If any command list requires not rebuilding the buffer, dont delete the command.
                // For now, not likely
                delete cmd;
                cmd = next;
            }
        }
    }

    void ogl_graphics_driver::abort() {
        list_queue.abort();
        should_stop = true;
    }
}
