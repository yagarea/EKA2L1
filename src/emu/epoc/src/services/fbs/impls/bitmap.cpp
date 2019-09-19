/*
 * Copyright (c) 2019 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 * 
 * Initial contributor: pent0
 * Contributors:
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
#include <common/cvt.h>
#include <common/fileutils.h>
#include <common/log.h>

#include <epoc/services/fbs/fbs.h>
#include <epoc/services/fs/fs.h>
#include <epoc/services/window/common.h>

#include <epoc/epoc.h>
#include <epoc/kernel.h>
#include <epoc/vfs.h>

#include <common/e32inc.h>
#include <e32err.h>

#include <cassert>

namespace eka2l1 {
    static epoc::display_mode get_display_mode_from_bpp(const int bpp) {
        switch (bpp) {
        case 1:
            return epoc::display_mode::gray2;
        case 2:
            return epoc::display_mode::gray4;
        case 4:
            return epoc::display_mode::color16;
        case 8:
            return epoc::display_mode::color256;
        case 12:
            return epoc::display_mode::color4k;
        case 16:
            return epoc::display_mode::color64k;
        case 24:
            return epoc::display_mode::color16m;
        case 32:
            return epoc::display_mode::color16ma;
        default:
            break;
        }

        return epoc::display_mode::color16m;
    }

    static int get_bpp_from_display_mode(const epoc::display_mode bpp) {
        switch (bpp) {
        case epoc::display_mode::gray2:
            return 1;
        case epoc::display_mode::gray4:
            return 2;
        case epoc::display_mode::gray16:
        case epoc::display_mode::color16:
            return 4;
        case epoc::display_mode::gray256:
        case epoc::display_mode::color256:
            return 8;
        case epoc::display_mode::color4k:
            return 12;
        case epoc::display_mode::color64k:
            return 16;
        case epoc::display_mode::color16m:
            return 24;
        case epoc::display_mode::color16mu:
        case epoc::display_mode::color16ma:
            return 32;
        }

        return 24;
    }

    static epoc::bitmap_color get_bitmap_color_type_from_display_mode(const epoc::display_mode bpp) {
        switch (bpp) {
        case epoc::display_mode::gray2:
            return epoc::monochrome_bitmap;
        case epoc::display_mode::color16ma:
        case epoc::display_mode::color16map:
            return epoc::color_bitmap_with_alpha;
        default:
            break;
        }

        return epoc::color_bitmap;
    }

    static int get_byte_width(const std::uint32_t pixels_width, const std::uint8_t bits_per_pixel) {
        int word_width = 0;

        switch (bits_per_pixel) {
        case 1: {
            word_width = (pixels_width + 31) / 32;
            break;
        }

        case 2: {
            word_width = (pixels_width + 15) / 16;
            break;
        }

        case 4: {
            word_width = (pixels_width + 7) / 8;
            break;
        }

        case 8: {
            word_width = (pixels_width + 3) / 4;
            break;
        }

        case 12:
        case 16: {
            word_width = (pixels_width + 1) / 2;
            break;
        }

        case 24: {
            word_width = (((pixels_width * 3) + 11) / 12) * 3;
            break;
        }

        case 32: {
            word_width = pixels_width;
            break;
        }

        default: {
            assert(false);
            break;
        }
        }

        return word_width * 4;
    }

    namespace epoc {
        constexpr std::uint32_t BITWISE_BITMAP_UID = 0x10000040;
        constexpr std::uint32_t MAGIC_FBS_PILE_PTR = 0xDEADFB55;

        // Magic heap pointer so the client knows that we allocate from the large chunk.
        // The 8 is actually H, so you get my idea.
        constexpr std::uint32_t MAGIC_FBS_HEAP_PTR = 0xDEADFB88;

        display_mode bitwise_bitmap::settings::initial_display_mode() const {
            return static_cast<display_mode>(flags_ & 0x000000FF);
        }

        display_mode bitwise_bitmap::settings::current_display_mode() const {
            return static_cast<display_mode>((flags_ & 0x0000FF00) >> 8);
        }

        void bitwise_bitmap::settings::current_display_mode(const display_mode &mode) {
            flags_ &= 0xFFFF00FF;
            flags_ |= (static_cast<std::uint32_t>(mode) << 8);
        }

        void bitwise_bitmap::settings::initial_display_mode(const display_mode &mode) {
            flags_ &= 0xFFFFFF00;
            flags_ |= static_cast<std::uint32_t>(mode);
        }

        bool bitwise_bitmap::settings::dirty_bitmap() const {
            return flags_ & settings_flag::dirty_bitmap;
        }

        void bitwise_bitmap::settings::dirty_bitmap(const bool is_it) {
            if (is_it)
                flags_ &= settings_flag::dirty_bitmap;
            else
                flags_ &= ~(settings_flag::dirty_bitmap);
        }

        bool bitwise_bitmap::settings::violate_bitmap() const {
            return flags_ & settings_flag::violate_bitmap;
        }

        void bitwise_bitmap::settings::violate_bitmap(const bool is_it) {
            if (is_it)
                flags_ &= settings_flag::violate_bitmap;
            else
                flags_ &= ~(settings_flag::violate_bitmap);
        }

        static void do_white_fill(std::uint8_t *dest, const std::size_t size, epoc::display_mode mode) {
            std::fill(dest, dest + size, 0xFF);
        }

        void bitwise_bitmap::construct(loader::sbm_header &info, void *data, const void *base, const bool white_fill) {
            uid_ = epoc::BITWISE_BITMAP_UID;
            allocator_ = MAGIC_FBS_HEAP_PTR;
            pile_ = MAGIC_FBS_PILE_PTR;
            byte_width_ = 0;
            header_ = info;
            spare1_ = 0;
            data_offset_ = 0xFFFFFF;

            if (info.compression != epoc::bitmap_file_no_compression) {
                // Compressed in RAM!
                compressed_in_ram_ = true;
            }

            data_offset_ = static_cast<int>(reinterpret_cast<const std::uint8_t *>(data) - reinterpret_cast<const std::uint8_t *>(base));

            const epoc::display_mode disp_pixel_mode = get_display_mode_from_bpp(info.bit_per_pixels);
            settings_.current_display_mode(disp_pixel_mode);
            settings_.initial_display_mode(disp_pixel_mode);

            byte_width_ = get_byte_width(info.size_pixels.width(), static_cast<std::uint8_t>(info.bit_per_pixels));

            if (white_fill) {
                do_white_fill(reinterpret_cast<std::uint8_t *>(data), info.bitmap_size - sizeof(loader::sbm_header), settings_.current_display_mode());
            }
        }
    }

    struct load_bitmap_arg {
        std::uint32_t bitmap_id;
        std::int32_t share;
        std::int32_t file_offset;
    };

    struct bmp_handles {
        std::int32_t handle;
        std::int32_t server_handle;
        std::int32_t address_offset;
    };

    fbsbitmap::~fbsbitmap() {
        serv_->free_bitmap(this);
    }

    std::optional<std::size_t> fbs_server::load_uncomp_data_to_rom(loader::mbm_file &mbmf_, const std::size_t idx_, int *err_code) {
        // First, get the size of data when uncompressed
        std::size_t size_when_uncompressed = 0;
        if (!mbmf_.read_single_bitmap(idx_, nullptr, size_when_uncompressed)) {
            *err_code = fbs_load_data_err_read_decomp_fail;
            return std::nullopt;
        }

        // Allocates from the large chunk
        // Align them with 4 bytes
        std::size_t avail_dest_size = common::align(size_when_uncompressed, 4);
        void *data = large_chunk_allocator->allocate(avail_dest_size);
        if (data == nullptr) {
            // We can't allocate at all
            *err_code = fbs_load_data_err_out_of_mem;
            return std::nullopt;
        }

        // Yay, we manage to alloc memory to load the data in
        // So let's get to work
        if (!mbmf_.read_single_bitmap(idx_, reinterpret_cast<std::uint8_t *>(data), avail_dest_size)) {
            *err_code = fbs_load_data_err_read_decomp_fail;
            return std::nullopt;
        }

        *err_code = fbs_load_data_err_none;
        return reinterpret_cast<std::uint8_t *>(data) - base_large_chunk;
    }

    void fbscli::load_bitmap(service::ipc_context *ctx) {
        // Get the FS session
        session_ptr fs_target_session = ctx->sys->get_kernel_system()->get<service::session>(*(ctx->get_arg<int>(2)));
        const std::uint32_t fs_file_handle = static_cast<std::uint32_t>(*(ctx->get_arg<int>(3)));

        auto fs_server = std::reinterpret_pointer_cast<eka2l1::fs_server>(server<fbs_server>()->fs_server);
        file *source_file = fs_server->get_file(fs_target_session->unique_id(), fs_file_handle);

        if (!source_file) {
            ctx->set_request_status(KErrArgument);
            return;
        }

        load_bitmap_impl(ctx, source_file);
    }

    void fbscli::duplicate_bitmap(service::ipc_context *ctx) {
        fbsbitmap *bmp = server<fbs_server>()->get<fbsbitmap>(static_cast<std::uint32_t>(*(ctx->get_arg<int>(0))));
        if (!bmp) {
            ctx->set_request_status(KErrBadHandle);
            return;
        }

        bmp_handles handle_info;

        // Add this object to the object table!
        handle_info.handle = obj_table_.add(bmp);
        handle_info.server_handle = bmp->id;
        handle_info.address_offset = server<fbs_server>()->host_ptr_to_guest_shared_offset(bmp->bitmap_);

        ctx->write_arg_pkg(1, handle_info);
        ctx->set_request_status(KErrNone);
    }

    void fbscli::load_bitmap_impl(service::ipc_context *ctx, file *source) {
        std::optional<load_bitmap_arg> load_options = ctx->get_arg_packed<load_bitmap_arg>(0);
        if (!load_options) {
            ctx->set_request_status(KErrArgument);
            return;
        }

        LOG_TRACE("Loading bitmap from: {}", common::ucs2_to_utf8(source->file_name()));

        fbsbitmap *bmp = nullptr;
        fbs_server *fbss = server<fbs_server>();

        fbsbitmap_cache_info cache_info_;

        // Check if it's shared first
        if (load_options->share) {
            // Shared bitmaps are stored on server's map, because it's means to be accessed by
            // other fbs clients. Let's lookup our bitmap on there
            cache_info_.bitmap_idx = load_options->bitmap_id;
            cache_info_.last_access_time_since_ad = source->last_modify_since_1ad();
            cache_info_.path = source->file_name();

            auto shared_bitmap_ite = fbss->shared_bitmaps.find(cache_info_);

            if (shared_bitmap_ite != fbss->shared_bitmaps.end()) {
                bmp = shared_bitmap_ite->second;
            }
        }

        if (!bmp) {
            // Let's load the MBM from file first
            eka2l1::ro_file_stream stream_(source);
            loader::mbm_file mbmf_(reinterpret_cast<common::ro_stream *>(&stream_));

            mbmf_.do_read_headers();

            // Let's do an insanity check. Is the bitmap index client given us is not valid ?
            // What i mean, maybe it's out of range. There may be only 5 bitmaps, but client gives us index 6.
            if (mbmf_.trailer.count < load_options->bitmap_id) {
                ctx->set_request_status(KErrNotFound);
                return;
            }

            // With doing that, we can now finally start loading to bitmap properly. So let's do it,
            // hesistate is bad.
            epoc::bitwise_bitmap *bws_bmp = fbss->allocate_general_data<epoc::bitwise_bitmap>();
            bws_bmp->header_ = mbmf_.sbm_headers[load_options->bitmap_id - 1];

            // Load the bitmap data to large chunk
            int err_code = fbs_load_data_err_none;
            auto bmp_data_offset = fbss->load_uncomp_data_to_rom(mbmf_, load_options->bitmap_id - 1, &err_code);

            if (!bmp_data_offset) {
                switch (err_code) {
                case fbs_load_data_err_out_of_mem: {
                    LOG_ERROR("Can't allocate data for storing bitmap!");
                    ctx->set_request_status(KErrNoMemory);

                    return;
                }

                case fbs_load_data_err_read_decomp_fail: {
                    LOG_ERROR("Can't read or decompress bitmap data, possibly corrupted.");
                    ctx->set_request_status(KErrCorrupt);

                    return;
                }

                default: {
                    LOG_ERROR("Unknown error code from loading uncompressed bitmap!");
                    ctx->set_request_status(KErrGeneral);

                    return;
                }
                }
            }

            // Place holder value indicates we allocate through the large chunk.
            // If guest uses this, than we are doom. But gonna put it here anyway
            bws_bmp->pile_ = epoc::MAGIC_FBS_PILE_PTR;
            bws_bmp->allocator_ = epoc::MAGIC_FBS_HEAP_PTR;

            bws_bmp->data_offset_ = static_cast<std::uint32_t>(bmp_data_offset.value());
            bws_bmp->compressed_in_ram_ = false;
            bws_bmp->byte_width_ = get_byte_width(bws_bmp->header_.size_pixels.x, bws_bmp->header_.bit_per_pixels);
            bws_bmp->uid_ = epoc::bitwise_bitmap_uid;

            // The header still has compression type set maybe. Since right now we are decompressing it,
            // let's set them to no compression
            // TODO: Make this more dynamic.
            bws_bmp->header_.compression = epoc::bitmap_file_no_compression;
            bws_bmp->header_.bitmap_size = sizeof(loader::sbm_header) + bws_bmp->byte_width_ * bws_bmp->header_.size_pixels.height();

            // Get display mode
            const epoc::display_mode dpm = get_display_mode_from_bpp(bws_bmp->header_.bit_per_pixels);
            bws_bmp->settings_.initial_display_mode(dpm);
            bws_bmp->settings_.current_display_mode(dpm);

            bmp = make_new<fbsbitmap>(fbss, bws_bmp, static_cast<bool>(load_options->share), support_dirty_bitmap);
        }

        if (load_options->share) {
            fbss->shared_bitmaps.emplace(cache_info_, bmp);
        }

        // Now writes the bitmap info in
        bmp_handles handle_info;

        // Add this object to the object table!
        handle_info.handle = obj_table_.add(bmp);
        handle_info.server_handle = bmp->id;
        handle_info.address_offset = fbss->host_ptr_to_guest_shared_offset(bmp->bitmap_);

        ctx->write_arg_pkg<bmp_handles>(0, handle_info);
        ctx->set_request_status(KErrNone);
    }

    struct bmp_specs {
        eka2l1::vec2 size;
        epoc::display_mode bpp; // Ignore
        std::uint32_t handle;
        std::uint32_t server_handle;
        std::uint32_t address_offset;
    };

    static std::size_t calculate_aligned_bitmap_bytes(const eka2l1::vec2 &size, const epoc::display_mode bpp) {
        if (size.x == 0 || size.y == 0) {
            return 0;
        }

        return get_byte_width(size.x, get_bpp_from_display_mode(bpp)) * size.y;
    }

    fbsbitmap *fbs_server::create_bitmap(const eka2l1::vec2 &size, const epoc::display_mode dpm, const bool support_dirty) {
        epoc::bitwise_bitmap *bws_bmp = allocate_general_data<epoc::bitwise_bitmap>();

        // Calculate the size
        std::size_t alloc_bytes = calculate_aligned_bitmap_bytes(size, dpm);
        void *data = nullptr;

        if (alloc_bytes > 0) {
            // Allocates from the large chunk
            // Align them with 4 bytes
            std::size_t avail_dest_size = common::align(alloc_bytes, 4);
            data = large_chunk_allocator->allocate(avail_dest_size);

            if (!data) {
                shared_chunk_allocator->free(bws_bmp);
                return nullptr;
            }
        }

        loader::sbm_header header;
        header.compression = epoc::bitmap_file_no_compression;
        header.bitmap_size = alloc_bytes + sizeof(loader::sbm_header);
        header.size_pixels = size;
        header.color = get_bitmap_color_type_from_display_mode(dpm);
        header.header_len = sizeof(loader::sbm_header);
        header.palette_size = 0;
        header.size_twips = size * twips_mul;
        header.bit_per_pixels = get_bpp_from_display_mode(dpm);

        bws_bmp->construct(header, data, base_large_chunk, true);

        fbsbitmap *bmp = make_new<fbsbitmap>(this, bws_bmp, false, support_dirty);
        return bmp;
    }

    bool fbs_server::free_bitmap(fbsbitmap *bmp) {
        if (!bmp->bitmap_ || bmp->count > 0) {
            return false;
        }

        // First, free the bitmap pixels.
        if (!large_chunk_allocator->free(base_large_chunk + bmp->bitmap_->data_offset_)) {
            return false;
        }

        // Free the bitwise bitmap.
        if (!free_general_data(bmp->bitmap_)) {
            return false;
        }

        return true;
    }

    void fbscli::create_bitmap(service::ipc_context *ctx) {
        std::optional<bmp_specs> specs = ctx->get_arg_packed<bmp_specs>(0);

        if (!specs) {
            ctx->set_request_status(KErrArgument);
            return;
        }

        fbs_server *fbss = server<fbs_server>();
        fbsbitmap *bmp = fbss->create_bitmap(specs->size, specs->bpp);

        if (!bmp) {
            ctx->set_request_status(KErrNoMemory);
            return;
        }

        specs->handle = obj_table_.add(bmp);
        specs->server_handle = bmp->id;
        specs->address_offset = fbss->host_ptr_to_guest_shared_offset(bmp->bitmap_);

        ctx->write_arg_pkg(0, specs.value());
        ctx->set_request_status(KErrNone);
    }

    void fbscli::notify_dirty_bitmap(service::ipc_context *ctx) {
        if (!nof_) {
            server<fbs_server>()->dirty_nofs.push_back({ this, epoc::notify_info(ctx->msg->request_sts, ctx->msg->own_thr) });
            nof_ = &server<fbs_server>()->dirty_nofs.back();
        }

        if (nof_->dirty) {
            ctx->set_request_status(KErrNone);
            return;
        }

        nof_->dirty = false;
        nof_->nof.sts = ctx->msg->request_sts;
        nof_->nof.requester = ctx->msg->own_thr;
    }

    void fbscli::cancel_notify_dirty_bitmap(service::ipc_context *ctx) {
        std::vector<fbs_dirty_notify_request> &notifies = server<fbs_server>()->dirty_nofs;

        for (std::size_t i = 0; i < notifies.size(); i++) {
            if (notifies[i].client == this) {
                // Officially cancel the request
                notifies[i].nof.complete(KErrCancel);
                notifies.erase(notifies.begin() + i);
            }
        }

        nof_ = nullptr;
        ctx->set_request_status(KErrNone);
    }

    void fbscli::get_clean_bitmap(service::ipc_context *ctx) {
        const epoc::handle bmp_handle = static_cast<epoc::handle>(*(ctx->get_arg<int>(0)));
        fbsbitmap *bmp = obj_table_.get<fbsbitmap>(bmp_handle);

        if (!bmp) {
            ctx->set_request_status(KErrBadHandle);
            return;
        }

        while (bmp->clean_bitmap != nullptr) {
            bmp = bmp->clean_bitmap;
        }

        // Close the old handle
        obj_table_.remove(bmp_handle);

        bmp_handles handle_info;

        // Get the clean bitmap handle!
        handle_info.handle = obj_table_.add(bmp);
        handle_info.server_handle = bmp->id;
        handle_info.address_offset = server<fbs_server>()->host_ptr_to_guest_shared_offset(bmp->bitmap_);

        ctx->write_arg_pkg(1, handle_info);
        ctx->set_request_status(KErrNone);
    }
}
