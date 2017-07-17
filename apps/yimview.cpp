//
// LICENSE:
//
// Copyright (c) 2016 -- 2017 Fabio Pellacini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include "../yocto/yocto_glu.h"
#include "../yocto/yocto_gui.h"
#include "../yocto/yocto_img.h"
#include "../yocto/yocto_math.h"
#include "../yocto/yocto_utils.h"

namespace yimview_app {

struct img {
    // image path
    std::string filename;

    // original image data size
    int width = 0;
    int height = 0;
    int ncomp = 0;

    // pixel data
    float* hdr = nullptr;
    unsigned char* ldr = nullptr;

    // opengl texture
    yglu::uint tex_glid = 0;

    // hdr controls
    float exposure = 0;
    float gamma = 2.2f;
    bool srgb = true;
    ym::tonemap_type tonemap = ym::tonemap_type::srgb;

    // check hdr
    bool is_hdr() const { return (bool)hdr; }

    // cleanup
    ~img() {
        if (hdr) delete hdr;
        if (ldr) delete ldr;
    }
};

struct params {
    std::vector<std::string> filenames;
    std::vector<img*> imgs;

    float exposure = 0;
    float gamma = 1;
    ym::tonemap_type tonemap = ym::tonemap_type::gamma;

    int cur_img = 0;
    int cur_background = 0;
    float zoom = 1;
    ym::vec2f offset = ym::vec2f();

    float background = 0;

    void* widget_ctx = nullptr;

    ~params() {
        for (auto img : imgs) delete img;
    }
};

std::vector<img*> load_images(const std::vector<std::string>& img_filenames,
    float exposure, ym::tonemap_type tonemap, float gamma) {
    auto imgs = std::vector<img*>();
    for (auto filename : img_filenames) {
        imgs.push_back(new img());
        auto img = imgs.back();
        img->filename = filename;
        auto ext = yu::path::get_extension(filename);
        if (ext == ".hdr") {
            img->hdr = yimg::load_imagef(
                filename, img->width, img->height, img->ncomp);

        } else {
            img->ldr =
                yimg::load_image(filename, img->width, img->height, img->ncomp);
        }
        if (img->hdr) {
            img->ldr = new unsigned char[img->width * img->height * img->ncomp];
            ym::tonemap_image(img->width, img->height, img->ncomp, img->hdr,
                img->ldr, img->tonemap, img->exposure, img->gamma);
            img->exposure = exposure;
            img->gamma = gamma;
            img->tonemap = tonemap;
        }
        if (!img->hdr && !img->ldr) {
            printf("cannot load image %s\n", img->filename.c_str());
            exit(1);
        }
        img->tex_glid = 0;
    }
    return imgs;
}

void init_params(params* pars, yu::cmdline::parser* parser) {
    static auto tmtype_names = std::vector<std::pair<std::string, int>>{
        {"none", (int)ym::tonemap_type::none},
        {"srgb", (int)ym::tonemap_type::srgb},
        {"gamma", (int)ym::tonemap_type::gamma},
        {"filmic", (int)ym::tonemap_type::filmic}};

    pars->exposure =
        parse_optf(parser, "--exposure", "-e", "hdr image exposure", 0);
    pars->gamma = parse_optf(parser, "--gamma", "-g", "hdr image gamma", 2.2f);
    pars->tonemap = (ym::tonemap_type)parse_opte(parser, "--tonemap", "-t",
        "hdr image tonemap", (int)ym::tonemap_type::srgb, tmtype_names);
    auto filenames = parse_argas(parser, "image", "image filename", {}, true);

    // loading images
    pars->imgs =
        load_images(filenames, pars->exposure, pars->tonemap, pars->gamma);
}

}  // namespace yimview_app

const int hud_width = 256;

void text_callback(ygui::window* win, unsigned int key) {
    auto pars = (yimview_app::params*)get_user_pointer(win);
    switch (key) {
        case ' ':
        case '.':
            pars->cur_img = (pars->cur_img + 1) % pars->imgs.size();
            break;
        case ',':
            pars->cur_img = (pars->cur_img - 1 + (int)pars->imgs.size()) %
                            pars->imgs.size();
            break;
        case '-':
        case '_': pars->zoom /= 2; break;
        case '+':
        case '=': pars->zoom *= 2; break;
        case '[': pars->exposure -= 1; break;
        case ']': pars->exposure += 1; break;
        case '{': pars->gamma -= 0.1f; break;
        case '}': pars->gamma += 0.1f; break;
        case '1':
            pars->exposure = 0;
            pars->gamma = 1;
            break;
        case '2':
            pars->exposure = 0;
            pars->gamma = 2.2f;
            break;
        case 'z': pars->zoom = 1; break;
        case 'h':
            // TODO: hud
            break;
        default: printf("unsupported key\n"); break;
    }
}

void draw_image(ygui::window* win) {
    auto pars = (yimview_app::params*)get_user_pointer(win);
    auto framebuffer_size = get_framebuffer_size(win);
    yglu::set_viewport({0, 0, framebuffer_size[0], framebuffer_size[1]});

    auto img = pars->imgs[pars->cur_img];

    // begin frame
    yglu::clear_buffers(
        {pars->background, pars->background, pars->background, 0});

    // draw image
    auto window_size = get_window_size(win);
    yglu::shade_image(img->tex_glid, img->width, img->height, window_size[0],
        window_size[1], pars->offset[0], pars->offset[1], pars->zoom);
}

template <typename T>
ym::vec<T, 4> lookup_image(
    int w, int h, int nc, const T* pixels, int x, int y, T one) {
    if (x < 0 || y < 0 || x > w - 1 || y > h - 1) return {0, 0, 0, 0};
    auto v = ym::vec<T, 4>{0, 0, 0, 0};
    auto vv = pixels + ((w * y) + x) * nc;
    switch (nc) {
        case 1: v = {vv[0], 0, 0, one}; break;
        case 2: v = {vv[0], vv[1], 0, one}; break;
        case 3: v = {vv[0], vv[1], vv[2], one}; break;
        case 4: v = {vv[0], vv[1], vv[2], vv[3]}; break;
        default: assert(false);
    }
    return v;
}

void draw_widgets(ygui::window* win) {
    static auto tmtype_names = std::vector<std::pair<std::string, int>>{
        {"none", (int)ym::tonemap_type::none},
        {"srgb", (int)ym::tonemap_type::srgb},
        {"gamma", (int)ym::tonemap_type::gamma},
        {"filmic", (int)ym::tonemap_type::filmic}};

    auto pars = (yimview_app::params*)get_user_pointer(win);
    auto& img = pars->imgs[pars->cur_img];
    auto mouse_pos = (ym::vec2f)get_mouse_posf(win);
    if (begin_widgets(win, "yimview")) {
        label_widget(win, "filename", img->filename);
        label_widget(win, "w", img->width);
        label_widget(win, "h", img->height);
        label_widget(win, "c", img->ncomp);
        auto xy = (mouse_pos - pars->offset) / pars->zoom;
        auto ij = ym::vec2i{(int)round(xy[0]), (int)round(xy[1])};
        auto inside = ij[0] >= 0 && ij[1] >= 0 && ij[0] < img->width &&
                      ij[1] < img->height;
        auto ldrp = lookup_image(img->width, img->height, img->ncomp, img->ldr,
            ij[0], ij[1], (unsigned char)255);
        label_widget(win, "r", (inside) ? ldrp[0] : 0);
        label_widget(win, "g", (inside) ? ldrp[1] : 0);
        label_widget(win, "b", (inside) ? ldrp[2] : 0);
        label_widget(win, "a", (inside) ? ldrp[3] : 0);
        if (img->is_hdr()) {
            auto hdrp = lookup_image(img->width, img->height, img->ncomp,
                img->hdr, ij[0], ij[1], 1.0f);
            label_widget(win, "r", (inside) ? hdrp[0] : 0);
            label_widget(win, "g", (inside) ? hdrp[1] : 0);
            label_widget(win, "b", (inside) ? hdrp[2] : 0);
            label_widget(win, "a", (inside) ? hdrp[3] : 0);
            slider_widget(win, "exposure", &pars->exposure, -20, 20, 1);
            slider_widget(win, "gamma", &pars->gamma, 0.1, 5, 0.1);
            combo_widget(win, "tonemap", (int*)&pars->tonemap, tmtype_names);
        }
    }
    end_widgets(win);
}

void window_refresh_callback(ygui::window* win) {
    draw_image(win);
    draw_widgets(win);
    swap_buffers(win);
}

void run_ui(yimview_app::params* pars) {
    // window
    auto win = ygui::init_window(pars->imgs[0]->width + hud_width,
        pars->imgs[0]->height, "yimview", pars);
    set_callbacks(win, text_callback, nullptr, window_refresh_callback);

    // window values
    int mouse_button = 0;
    ym::vec2f mouse_pos, mouse_last;

    init_widgets(win);

    // load textures
    for (auto& img : pars->imgs) {
        img->tex_glid = yglu::make_texture(img->width, img->height, img->ncomp,
            (unsigned char*)img->ldr, false, false, false);
    }

    while (!should_close(win)) {
        mouse_last = mouse_pos;
        mouse_pos = get_mouse_posf(win);
        mouse_button = get_mouse_button(win);

        auto& img = pars->imgs[pars->cur_img];
        set_window_title(win,
            ("yimview | " + img->filename + " | " + std::to_string(img->width) +
                "x" + std::to_string(img->height) + "@" +
                std::to_string(img->ncomp))
                .c_str());

        // handle mouse
        if (mouse_button && mouse_pos != mouse_last &&
            !get_widget_active(win)) {
            switch (mouse_button) {
                case 1: pars->offset += mouse_pos - mouse_last; break;
                case 2:
                    pars->zoom *=
                        powf(2, (mouse_pos[0] - mouse_last[0]) * 0.001f);
                    break;
                default: break;
            }
        }

        // refresh hdr
        if (img->is_hdr() &&
            (pars->exposure != img->exposure || pars->gamma != img->gamma ||
                pars->tonemap != img->tonemap)) {
            ym::tonemap_image(img->width, img->height, img->ncomp, img->hdr,
                img->ldr, pars->tonemap, pars->exposure, pars->gamma);
            img->exposure = pars->exposure;
            img->gamma = pars->gamma;
            img->tonemap = pars->tonemap;
            yglu::update_texture(img->tex_glid, img->width, img->height,
                img->ncomp, img->ldr, false);
        }

        // draw
        draw_image(win);
        draw_widgets(win);

        // swap buffers
        swap_buffers(win);

        // event hadling
        wait_events(win);
    }

    clear_widgets(win);
    clear_window(win);
}

int main(int argc, char* argv[]) {
    // command line params
    auto pars = new yimview_app::params();
    auto parser = yu::cmdline::make_parser(argc, argv, "view images");
    yimview_app::init_params(pars, parser);
    check_parser(parser);

    // run ui
    run_ui(pars);

    // done
    delete pars;
    return EXIT_SUCCESS;
}
