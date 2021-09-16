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

#include <android/bitmap.h>
#include <android/launcher.h>
#include <android/state.h>

#include <package/manager.h>
#include <system/devices.h>

#include <common/fileutils.h>
#include <common/language.h>
#include <common/path.h>
#include <loader/mif.h>
#include <loader/svgb.h>
#include <services/fbs/fbs.h>
#include <system/installation/firmware.h>
#include <system/installation/rpkg.h>
#include <utils/locale.h>
#include <utils/system.h>

#include <document.h>

namespace eka2l1::android {

    launcher::launcher(eka2l1::system *sys)
        : sys(sys)
        , conf(sys->get_config())
        , kern(sys->get_kernel_system())
        , alserv(nullptr) {
        if (kern) {
            alserv = reinterpret_cast<eka2l1::applist_server *>(kern->get_by_name<service::server>(get_app_list_server_name_by_epocver(
                kern->get_epoc_version())));
            winserv = reinterpret_cast<eka2l1::window_server *>(kern->get_by_name<service::server>(get_winserv_name_by_epocver(
                kern->get_epoc_version())));
            fbsserv = reinterpret_cast<eka2l1::fbs_server *>(kern->get_by_name<service::server>(epoc::get_fbs_server_name_by_epocver(
                kern->get_epoc_version())));
        }
    }

    std::vector<std::string> launcher::get_apps() {
        std::vector<apa_app_registry> &registerations = alserv->get_registerations();
        std::vector<std::string> info;
        for (auto &reg : registerations) {
            if (!reg.caps.is_hidden) {
                std::string name = common::ucs2_to_utf8(reg.mandatory_info.long_caption.to_std_string(nullptr));
                std::string uid = std::to_string(reg.mandatory_info.uid);
                info.push_back(uid);
                info.push_back(name);
            }
        }
        return info;
    }

    static jobject make_new_bitmap(JNIEnv *env, std::uint32_t width, std::uint32_t height) {
        jclass bitmapCls = env->FindClass("android/graphics/Bitmap");
        jmethodID createBitmapFunction = env->GetStaticMethodID(bitmapCls,
            "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
        jstring configName = env->NewStringUTF("ARGB_8888");
        jclass bitmapConfigClass = env->FindClass("android/graphics/Bitmap$Config");
        jmethodID valueOfBitmapConfigFunction = env->GetStaticMethodID(
            bitmapConfigClass, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;");

        jobject bitmapConfig = env->CallStaticObjectMethod(bitmapConfigClass, valueOfBitmapConfigFunction,
            configName);

        jobject newBitmap = env->CallStaticObjectMethod(bitmapCls, createBitmapFunction, width,
            height, bitmapConfig);

        return newBitmap;
    }

    jobjectArray launcher::get_app_icon(JNIEnv *env, std::uint32_t uid) {
        apa_app_registry *reg = alserv->get_registration(uid);
        if (!reg) {
            return nullptr;
        }

        jobjectArray jicons = env->NewObjectArray(2,
            env->FindClass("android/graphics/Bitmap"),
            nullptr);

        std::string app_name = common::ucs2_to_utf8(reg->mandatory_info.long_caption.to_std_string(nullptr));
        io_system *io = sys->get_io_system();

        const std::u16string path_ext = eka2l1::common::lowercase_ucs2_string(eka2l1::path_extension(reg->icon_file_path));

        if (path_ext == u".mif") {
            eka2l1::symfile file_route = io->open_file(reg->icon_file_path, READ_MODE | BIN_MODE);
            eka2l1::create_directories("cache");

            if (file_route) {
                eka2l1::ro_file_stream file_route_stream(file_route.get());
                eka2l1::loader::mif_file file_mif_parser(reinterpret_cast<eka2l1::common::ro_stream *>(&file_route_stream));

                if (file_mif_parser.do_parse()) {
                    std::vector<std::uint8_t> data;
                    int dest_size = 0;
                    if (file_mif_parser.read_mif_entry(0, nullptr, dest_size)) {
                        data.resize(dest_size);
                        file_mif_parser.read_mif_entry(0, data.data(), dest_size);

                        const std::string cached_path = fmt::format("cache//debinarized_{}.svg", app_name);

                        eka2l1::common::ro_buf_stream inside_stream(data.data(), data.size());
                        std::unique_ptr<eka2l1::common::wo_std_file_stream> outfile_stream = std::make_unique<eka2l1::common::wo_std_file_stream>(cached_path, true);

                        eka2l1::loader::mif_icon_header header;
                        inside_stream.read(&header, sizeof(eka2l1::loader::mif_icon_header));

                        std::vector<eka2l1::loader::svgb_convert_error_description> errors;

                        if (header.type == eka2l1::loader::mif_icon_type_svg) {
                            std::unique_ptr<lunasvg::Document> document;
                            if (!eka2l1::loader::convert_svgb_to_svg(inside_stream, *outfile_stream, errors)) {
                                if (errors[0].reason_ == eka2l1::loader::svgb_convert_error_invalid_file) {
                                    const char *char_data = reinterpret_cast<const char *>(data.data()) + sizeof(eka2l1::loader::mif_icon_header);
                                    document = lunasvg::Document::loadFromData(char_data, data.size() - sizeof(eka2l1::loader::mif_icon_header));
                                }
                            } else {
                                outfile_stream.reset();
                                document = lunasvg::Document::loadFromFile(cached_path.c_str());
                            }

                            if (document) {
                                std::uint32_t width = document->width();
                                std::uint32_t height = document->height();
                                jobject source_bitmap = make_new_bitmap(env, width, height);
                                void *data_to_write = nullptr;
                                int result = AndroidBitmap_lockPixels(env, source_bitmap, &data_to_write);
                                if (result < 0) {
                                    env->DeleteLocalRef(source_bitmap);
                                    env->DeleteLocalRef(jicons);
                                    return nullptr;
                                }

                                auto bitmap = lunasvg::Bitmap(reinterpret_cast<std::uint8_t *>(data_to_write), width, height, width * 4);
                                lunasvg::Matrix matrix{ 1, 0, 0, 1, 0, 0 };
                                document->render(bitmap, matrix, 0);

                                AndroidBitmap_unlockPixels(env, source_bitmap);

                                env->SetObjectArrayElement(jicons, 0, source_bitmap);
                                return jicons;
                            }
                        } else {
                            LOG_ERROR(eka2l1::FRONTEND_UI, "Unknown icon type {} for app {}", header.type, app_name);
                        }
                    }
                }
            }
        } else if (path_ext == u".mbm") {
            eka2l1::symfile file_route = io->open_file(reg->icon_file_path, READ_MODE | BIN_MODE);
            if (file_route) {
                eka2l1::ro_file_stream file_route_stream(file_route.get());
                eka2l1::loader::mbm_file file_mbm_parser(reinterpret_cast<eka2l1::common::ro_stream *>(&file_route_stream));

                if (file_mbm_parser.do_read_headers() && !file_mbm_parser.sbm_headers.empty()) {
                    eka2l1::loader::sbm_header *icon_header = &file_mbm_parser.sbm_headers[0];

                    jobject source_bitmap = make_new_bitmap(env, icon_header->size_pixels.x,
                        icon_header->size_pixels.y);
                    void *data_to_write = nullptr;
                    int result = AndroidBitmap_lockPixels(env, source_bitmap, &data_to_write);
                    if (result < 0) {
                        env->DeleteLocalRef(source_bitmap);
                        env->DeleteLocalRef(jicons);
                        return nullptr;
                    }

                    eka2l1::common::wo_buf_stream converted_write_stream(reinterpret_cast<std::uint8_t *>(data_to_write),
                        icon_header->size_pixels.x * icon_header->size_pixels.y * 4);

                    if (!eka2l1::epoc::convert_to_rgba8888(fbsserv, file_mbm_parser, 0, converted_write_stream)) {
                        env->DeleteLocalRef(source_bitmap);
                    } else {
                        AndroidBitmap_unlockPixels(env, source_bitmap);

                        env->SetObjectArrayElement(jicons, 0, source_bitmap);
                        return jicons;
                    }
                }
            }
        } else {
            std::optional<eka2l1::apa_app_masked_icon_bitmap> icon_pair = alserv->get_icon(*reg, 0);

            if (icon_pair.has_value()) {
                eka2l1::epoc::bitwise_bitmap *main_bitmap = icon_pair->first;
                jobject source_bitmap = make_new_bitmap(env, main_bitmap->header_.size_pixels.x,
                    main_bitmap->header_.size_pixels.y);
                void *data_to_write = nullptr;
                int result = AndroidBitmap_lockPixels(env, source_bitmap, &data_to_write);
                if (result < 0) {
                    env->DeleteLocalRef(source_bitmap);
                    env->DeleteLocalRef(jicons);
                    return nullptr;
                }

                eka2l1::common::wo_buf_stream main_bitmap_buf(reinterpret_cast<std::uint8_t *>(data_to_write),
                    main_bitmap->header_.size_pixels.x * main_bitmap->header_.size_pixels.y * 4);

                if (!eka2l1::epoc::convert_to_rgba8888(fbsserv, main_bitmap, main_bitmap_buf)) {
                    LOG_ERROR(eka2l1::FRONTEND_UI, "Unable to load main icon of app {}", app_name);
                    env->DeleteLocalRef(source_bitmap);
                } else {
                    AndroidBitmap_unlockPixels(env, source_bitmap);
                    env->SetObjectArrayElement(jicons, 0, source_bitmap);
                    if (icon_pair->second) {
                        eka2l1::epoc::bitwise_bitmap *second_bitmap = icon_pair->second;

                        jobject mask_bitmap = make_new_bitmap(env, main_bitmap->header_.size_pixels.x,
                            main_bitmap->header_.size_pixels.y);
                        result = AndroidBitmap_lockPixels(env, mask_bitmap, &data_to_write);
                        if (result < 0) {
                            env->DeleteLocalRef(mask_bitmap);
                            return jicons;
                        }

                        eka2l1::common::wo_buf_stream second_bitmap_buf(reinterpret_cast<std::uint8_t *>(data_to_write),
                            second_bitmap->header_.size_pixels.x * second_bitmap->header_.size_pixels.y * 4);

                        if (!eka2l1::epoc::convert_to_rgba8888(fbsserv, second_bitmap, second_bitmap_buf, true)) {
                            LOG_ERROR(eka2l1::FRONTEND_UI, "Unable to load mask bitmap icon of app {}", app_name);
                            env->DeleteLocalRef(mask_bitmap);
                        } else {
                            AndroidBitmap_unlockPixels(env, mask_bitmap);
                            env->SetObjectArrayElement(jicons, 1, mask_bitmap);
                        }
                    }
                    return jicons;
                }
            }
        }
        env->DeleteLocalRef(jicons);
        return nullptr;
    }

    void launcher::launch_app(std::uint32_t uid) {
        apa_app_registry *reg = alserv->get_registration(uid);

        epoc::apa::command_line cmdline;
        cmdline.launch_cmd_ = epoc::apa::command_create;
        kern->lock();
        alserv->launch_app(*reg, cmdline, nullptr);
        kern->unlock();
    }

    package::installation_result launcher::install_app(std::string &path) {
        std::u16string upath = common::utf8_to_ucs2(path);

        return sys->install_package(upath, drive_number::drive_e);
    }

    std::vector<std::string> launcher::get_devices() {
        device_manager *dvc_mngr = sys->get_device_manager();
        auto &dvcs = dvc_mngr->get_devices();
        std::vector<std::string> info;
        for (auto &device : dvcs) {
            std::string name = device.model;
            info.push_back(name);
        }
        return info;
    }

    void launcher::set_language_to_property(const language new_one) {
        property_ptr lang_prop = kern->get_prop(epoc::SYS_CATEGORY, epoc::LOCALE_LANG_KEY);
        auto current_lang = lang_prop->get_pkg<epoc::locale_language>();

        if (!current_lang) {
            return;
        }

        current_lang->language = static_cast<epoc::language>(new_one);
        lang_prop->set<epoc::locale_language>(current_lang.value());
    }

    void launcher::set_language_current(const language lang) {
        conf->language = static_cast<int>(lang);
        sys->set_system_language(lang);
        set_language_to_property(lang);
    }

    void launcher::set_current_device(std::uint32_t id) {
        device_manager *dvc_mngr = sys->get_device_manager();
        auto &dvcs = dvc_mngr->get_devices();

        if (conf->device != id) {
            // Check if the language currently in config exists in the new device
            if (std::find(dvcs[id].languages.begin(), dvcs[id].languages.end(), conf->language) == dvcs[id].languages.end()) {
                set_language_current(static_cast<language>(dvcs[id].default_language_code));
            }
        }

        conf->device = id;
        conf->serialize();
        dvc_mngr->set_current(id);
    }

    void launcher::set_device_name(std::uint32_t id, const char *name) {
        device_manager *dvc_mngr = sys->get_device_manager();
        auto &dvcs = dvc_mngr->get_devices();

        if (id < dvcs.size()) {
            dvcs[id].model = name;
            dvc_mngr->save_devices();
        }
    }

    std::uint32_t launcher::get_current_device() {
        return conf->device;
    }

    bool launcher::does_rom_need_rpkg(const std::string &rom_path) {
        return loader::should_install_requires_additional_rpkg(rom_path);
    }

    device_installation_error launcher::install_device(std::string &rpkg_path, std::string &rom_path, bool install_rpkg) {
        std::string firmware_code;
        device_manager *dvc_mngr = sys->get_device_manager();
        device_installation_error result;

        std::string root_c_path = add_path(conf->storage, "drives/c/");
        std::string root_e_path = add_path(conf->storage, "drives/e/");
        std::string root_z_path = add_path(conf->storage, "drives/z/");
        std::string rom_resident_path = add_path(conf->storage, "roms/");

        eka2l1::create_directories(rom_resident_path);

        bool need_add_rpkg = false;

        if (install_rpkg) {
            if (eka2l1::loader::should_install_requires_additional_rpkg(rom_path)) {
                result = eka2l1::loader::install_rpkg(dvc_mngr, rpkg_path, root_z_path, firmware_code, nullptr, nullptr);
                need_add_rpkg = true;
            } else {
                result = eka2l1::loader::install_rom(dvc_mngr, rom_path, rom_resident_path, root_z_path, nullptr, nullptr);
            }
        } else {
            result = eka2l1::install_firmware(
                dvc_mngr, rom_path, root_c_path, root_e_path, root_z_path, rom_resident_path,
                [](const std::vector<std::string> &variants) -> int { return 0; }, nullptr, nullptr);
        }

        if (result != device_installation_none) {
            return result;
        }

        dvc_mngr->save_devices();

        if (need_add_rpkg) {
            const std::string rom_directory = add_path(conf->storage, add_path("roms", firmware_code + "\\"));

            eka2l1::create_directories(rom_directory);
            common::copy_file(rom_path, add_path(rom_directory, "SYM.ROM"), true);
        }

        return device_installation_none;
    }

    std::vector<std::string> launcher::get_packages() {
        manager::packages *manager = sys->get_packages();
        std::vector<std::string> info;
        for (const auto &[pkg_uid, pkg] : *manager) {
            if (!pkg.is_removable) {
                continue;
            }
            std::string name = common::ucs2_to_utf8(pkg.package_name);
            std::string uid = std::to_string(pkg.uid);
            std::string index = std::to_string(pkg.index);
            info.push_back(uid);
            info.push_back(index);
            info.push_back(name);
        }
        return info;
    }

    void launcher::uninstall_package(std::uint32_t uid, std::int32_t ext_index) {
        manager::packages *manager = sys->get_packages();
        package::object *obj = manager->package(uid, ext_index);

        if (obj)
            manager->uninstall_package(*obj);
    }

    void launcher::mount_sd_card(std::string &path) {
        std::u16string upath = common::utf8_to_ucs2(path);

        io_system *io = sys->get_io_system();
        io->unmount(drive_e);
        io->mount_physical_path(drive_e, drive_media::physical,
            io_attrib_removeable | io_attrib_write_protected, upath);
    }

    void launcher::load_config() {
        conf->deserialize();
    }

    void launcher::set_language(std::uint32_t language_id) {
        set_language_current(static_cast<language>(language_id));
    }

    void launcher::set_rtos_level(std::uint32_t level) {
        kern->get_ntimer()->set_realtime_level(static_cast<realtime_level>(level));
    }

    void launcher::update_app_setting(std::uint32_t uid) {
        kern->get_app_settings()->update_setting(uid);
    }

    static void advance_dsa_pos_around_origin(eka2l1::rect &origin_normal_rect, const int rotation) {
        switch (rotation) {
        case 90:
            origin_normal_rect.top.x += origin_normal_rect.size.x;
            break;

        case 180:
            origin_normal_rect.top.x += origin_normal_rect.size.x;
            origin_normal_rect.top.y += origin_normal_rect.size.y;
            break;

        case 270:
            origin_normal_rect.top.y += origin_normal_rect.size.y;
            break;

        default:
            break;
        }
    }

    void launcher::draw(drivers::graphics_command_list_builder *builder, std::uint32_t window_width,
        std::uint32_t window_height) {
        epoc::screen *scr = winserv->get_screens();
        if (scr) {
            eka2l1::rect viewport;
            eka2l1::rect src;
            eka2l1::rect dest;

            drivers::filter_option filter = conf->nearest_neighbor_filtering ? drivers::filter_option::nearest : drivers::filter_option::linear;

            eka2l1::vec2 swapchain_size(window_width, window_height);
            viewport.size = swapchain_size;
            builder->set_swapchain_size(swapchain_size);

            builder->backup_state();
            builder->bind_bitmap(0);

            builder->clear({ 0xD0, 0xD0, 0xD0, 0xFF }, drivers::draw_buffer_bit_color_buffer);
            builder->set_cull_mode(false);
            builder->set_depth(false);
            //builder->set_clipping(true);
            builder->set_viewport(viewport);

            for (std::uint32_t i = 0; scr && scr->screen_texture; i++, scr = scr->next) {
                scr->screen_mutex.lock();
                auto &crr_mode = scr->current_mode();

                eka2l1::vec2 size = crr_mode.size;
                src.size = size;

                float mult = (float)(window_width) / size.x;
                float width = size.x * mult;
                float height = size.y * mult;
                std::uint32_t x = 0;
                std::uint32_t y = 0;
                if (height > swapchain_size.y) {
                    height = swapchain_size.y;
                    mult = height / size.y;
                    width = size.x * mult;
                    x = (swapchain_size.x - width) / 2;
                }
                scr->scale_x = mult;
                scr->scale_y = mult;
                scr->absolute_pos.x = static_cast<int>(x);
                scr->absolute_pos.y = static_cast<int>(y);

                dest.top = eka2l1::vec2(x, y);
                dest.size = eka2l1::vec2(width, height);

                advance_dsa_pos_around_origin(dest, scr->ui_rotation);

                if (scr->ui_rotation % 180 != 0) {
                    std::swap(dest.size.x, dest.size.y);
                    std::swap(src.size.x, src.size.y);
                }

                builder->set_texture_filter(scr->screen_texture, filter, filter);
                builder->draw_bitmap(scr->screen_texture, 0, dest, src, eka2l1::vec2(0, 0), static_cast<float>(scr->ui_rotation), eka2l1::drivers::bitmap_draw_flag_no_flip);

                scr->screen_mutex.unlock();
            }
            builder->load_backup_state();
        }
    }

    std::vector<std::string> launcher::get_language_ids() {
        std::vector<std::string> languages;

        device_manager *dvc_mngr = sys->get_device_manager();
        auto &dvcs = dvc_mngr->get_devices();
        if (!dvcs.empty()) {
            auto &dvc = dvcs[conf->device];
            for (int language : dvc.languages) {
                languages.push_back(std::to_string(language));
            }
        }
        return languages;
    }

    std::vector<std::string> launcher::get_language_names() {
        std::vector<std::string> languages;

        device_manager *dvc_mngr = sys->get_device_manager();
        auto &dvcs = dvc_mngr->get_devices();
        if (!dvcs.empty()) {
            auto &dvc = dvcs[conf->device];
            for (int language : dvc.languages) {
                const std::string lang_name = common::get_language_name_by_code(language);
                languages.push_back(lang_name);
            }
        }
        return languages;
    }
}
