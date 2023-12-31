#include "stdafx.h"
#include <thread>
#include <fmt/format.h>
#include "solver_registry.h"
#include "instruction.h"
#include "timer.h"
#include "similarity_checker.h"

// TODO: pragma dll load
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>



struct MouseParams;
using MouseParamsPtr = std::shared_ptr<MouseParams>;
struct MouseParams {
  int pe, px, py, pf;
  int e, x, y, f;
  MouseParams() { e = x = y = f = pe = px = py = pf = INT_MAX; };
  inline void load(int e_, int x_, int y_, int f_) {
    pe = e;
    px = x;
    py = y;
    pf = f;
    e = e_;
    x = x_;
    y = y_;
    f = f_;
  }
  inline bool clicked_left() const { return e == 1 && pe == 0; }
  inline bool clicked_right() const { return e == 2 && pe == 0; }
  inline bool released_left() const { return e == 4; }
  inline bool released_right() const { return e == 5; }
  inline bool drugging_left() const { return e == 0 && f == 1; }
  inline bool drugging_right() const { return e == 0 && f == 2; }
  inline std::pair<int, int> coord() const { return { x, y }; }
  inline std::pair<int, int> displacement() const {
    return { abs(x - px) > 10000 ? 0 : (x - px), abs(y - py) ? 0 : (y - py) };
  }
  std::string str() const {
    return fmt::format(
      "SMouseParams [(e,x,y,f)=({},{},{},{}), (pe,px,py,pf)=({},{},{},{})", e,
      x, y, f, pe, px, py, pf);
  }
  friend std::ostream& operator<<(std::ostream& o, const MouseParams& obj) {
    o << obj.str();
    return o;
  }
  friend std::ostream& operator<<(std::ostream& o, const MouseParamsPtr& obj) {
    o << obj->str();
    return o;
  }
  friend std::ostream& operator<<(std::ostream& o, const MouseParams* obj) {
    o << obj->str();
    return o;
  }
};

struct ManualParams;
using ManualParamsPtr = std::shared_ptr<ManualParams>;
struct ManualParams {
  // '1': Color
  // '2': Point
  // '3': Vertical
  // '4': Horizontal
  // '5': Swap
  // '6': Merge
  const std::vector<std::string> inst_name_map{
    "Nop", "Color", "Point", "Vertical", "Horizontal", "Swap", "Merge"
  };

  char inst_type = '1';
  int selected_frame_id = -1;
  std::string selected_block_id;

  std::string get_inst_name() const {
    return inst_name_map[inst_type - '0'];
  }
};

struct SeekBarVisualizer {

  static constexpr int mag = 2;
  static constexpr int info_height = 200;

  std::string winname = "img";

  Interpreter interpreter;
  PaintingPtr painting;

  std::vector<std::shared_ptr<Instruction>> instructions;
  std::vector<CanvasPtr> canvas_list;
  std::vector<int> inst_cost_list;
  std::vector<double> sim_cost_list;

  int frame_id = 0;
  int alpha = 0;
  bool draw_border_flag = true;
  bool heatmap_flag = false;
  cv::Mat_<cv::Vec4b> img;
  MouseParamsPtr mp;

  bool manual;
  ManualParamsPtr mpp = nullptr;

  SeekBarVisualizer(const PaintingPtr& painting, const CanvasPtr& initial_canvas, bool manual)
    : painting(painting), manual(manual)
  {
    mp = std::make_shared<MouseParams>();
    interpreter.top_level_id_counter = initial_canvas->calcTopLevelId();
    instructions.push_back(std::make_shared<NopInstruction>());
    canvas_list.push_back(initial_canvas);
    auto res = computeCost(*painting, canvas_list.front()->Clone(), instructions);
    inst_cost_list.push_back(res->instruction);
    sim_cost_list.push_back(res->similarity);
    if (manual) {
      LOG(INFO) << "create manual params";
      mpp = std::make_shared<ManualParams>();
    }
  }

  void read_instruction(const std::shared_ptr<Instruction>& inst) {
    auto canvas = canvas_list.back()->Clone();
    auto inst_cost = interpreter.Interpret(canvas, nullptr, inst)->cost;
    LOG(INFO) << fmt::format("instruction cost: {}", inst_cost);
    instructions.push_back(inst);
    canvas_list.push_back(canvas);
    auto res = computeCost(*painting, canvas_list.front()->Clone(), instructions);
    inst_cost_list.push_back(res->instruction);
    sim_cost_list.push_back(res->similarity);
  }

  void remove_successor_states() {
    instructions.erase(instructions.begin() + frame_id + 1, instructions.end());
    canvas_list.erase(canvas_list.begin() + frame_id + 1, canvas_list.end());
    inst_cost_list.erase(inst_cost_list.begin() + frame_id + 1, inst_cost_list.end());
    sim_cost_list.erase(sim_cost_list.begin() + frame_id + 1, sim_cost_list.end());
    fit_frame_trackbar();
  }

  void fit_frame_trackbar(bool move_newest = false) {
    cv::setTrackbarMax("frame id", winname, instructions.size() - 1);
    if (move_newest) {
      frame_id = instructions.size() - 1;
      cv::setTrackbarPos("frame id", winname, frame_id);
    }
  }

  Point get_logical_mouse_position() const {
    return { mp->x / mag, painting->height - (mp->y - info_height) / mag };
  }

  cv::Mat_<cv::Vec4b> create_board_image() {

    const int W = painting->width;
    const int H = painting->height;

    auto canvas = canvas_list[frame_id];

    std::vector<uint8_t> arr(W * mag * H * mag * 4, 255);
    const auto& pframe = painting->frame;
    auto cframe = Painter::draw(*canvas, false);
    if (heatmap_flag) {
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
          // 差が大きいなら BG を弱くする
          for (int i = 0; i < mag; ++i) {
            for (int j = 0; j < mag; ++j) {
              int diff = 0;
              for (int c = 0; c < 4; c++) diff += std::abs((int)pframe[W * y + x][c] - (int)cframe[W * y + x][c]);
              uint8_t val = 255 - diff / 4;
              arr[4 * (W * mag * (y * mag + i) + (x * mag + j)) + 0] = val;
              arr[4 * (W * mag * (y * mag + i) + (x * mag + j)) + 1] = val;
            }
          }
        }
      }
    }
    else {
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
          // BGRA
          for (int i = 0; i < mag; ++i) {
            for (int j = 0; j < mag; ++j) {
              arr[4 * (W * mag * (y * mag + i) + (x * mag + j)) + 0]
                = ((int)pframe[W * y + x][2] * alpha + (int)cframe[W * y + x][2] * (255 - alpha)) / 255;
              arr[4 * (W * mag * (y * mag + i) + (x * mag + j)) + 1]
                = ((int)pframe[W * y + x][1] * alpha + (int)cframe[W * y + x][1] * (255 - alpha)) / 255;
              arr[4 * (W * mag * (y * mag + i) + (x * mag + j)) + 2]
                = ((int)pframe[W * y + x][0] * alpha + (int)cframe[W * y + x][0] * (255 - alpha)) / 255;
              arr[4 * (W * mag * (y * mag + i) + (x * mag + j)) + 3]
                = ((int)pframe[W * y + x][3] * alpha + (int)cframe[W * y + x][3] * (255 - alpha)) / 255;
            }
          }
        }
      }
    }

    if (draw_border_flag) {
      const RGBA border_default_color{ 0, 255, 0, 255 };
      const RGBA border_emphasis_color{ 255, 0, 0, 255 };
      for (const auto& [block_id, block] : canvas->blocks) {
        assert(block);
        bool on_block = get_logical_mouse_position().isStrictlyInside(block->bottomLeft, block->topRight);
        auto border_color = on_block ? border_emphasis_color : border_default_color;

        // i* は画像座標
        std::vector<std::tuple<int, int>> border_pixels;
        int ixs[4] = {
          block->bottomLeft.px * mag,
          block->topRight.px * mag - 1,
          block->bottomLeft.px * mag + on_block,
          block->topRight.px * mag - 1 - on_block
        };
        for (int iy = block->bottomLeft.py * mag; iy < block->topRight.py * mag; ++iy) {
          for (auto ix : ixs) {
            border_pixels.emplace_back(ix, iy);
          }
        }
        int iys[4] = {
          block->bottomLeft.py * mag,
          block->topRight.py * mag - 1,
          block->bottomLeft.py * mag + on_block,
          block->topRight.py * mag - 1 - on_block
        };
        for (int ix = block->bottomLeft.px * mag; ix < block->topRight.px * mag; ++ix) {
          for (auto iy : iys) {
            border_pixels.emplace_back(ix, iy);
          }
        }
        for (auto [ix, iy] : border_pixels) {
          arr[4 * (W * mag * iy + ix) + 0] = border_color[2];
          arr[4 * (W * mag * iy + ix) + 1] = border_color[1];
          arr[4 * (W * mag * iy + ix) + 2] = border_color[0];
          arr[4 * (W * mag * iy + ix) + 3] = border_color[3];
        }
      }
    }

    // flip.
    for (int iy = 0; iy < H * mag / 2; ++iy) {
      uint8_t* src_line = &arr[4 * W * mag * iy];
      uint8_t* dst_line = &arr[4 * W * mag * (H * mag - 1 - iy)];
      for (int b = 0; b < 4 * W * mag; ++b) {
        std::swap(src_line[b], dst_line[b]);
      }
    }

    cv::Mat_<cv::Vec4b> img(H * mag, W * mag);
    std::memcpy(img.ptr(), arr.data(), sizeof(uint8_t) * 4 * H * mag * W * mag);

    return img;

  }

  cv::Mat_<cv::Vec4b> create_info_image() {
    const int width = painting->width * 2;
    std::vector<std::string> msgs;
    msgs.push_back(fmt::format("command: {}", instructions[frame_id]->toString()));
    msgs.push_back(fmt::format(" inst cost: {} (+ {})", inst_cost_list[frame_id], inst_cost_list[frame_id] - (frame_id >= 1 ? inst_cost_list[frame_id - 1] : 0)));
    msgs.push_back(fmt::format("  sim cost: {}", sim_cost_list[frame_id]));
    msgs.push_back(fmt::format("total cost: {}", inst_cost_list[frame_id] + sim_cost_list[frame_id]));
    msgs.push_back("");
    msgs.push_back(fmt::format("border(b): {}, heatmap(h): {}, logical (x, y)=({:3d}, {:3d})",
      draw_border_flag ? "on" : "off",
      heatmap_flag ? "on" : "off",
      get_logical_mouse_position().px, get_logical_mouse_position().py
    ));
    if (manual) {
      msgs.push_back(fmt::format("type: {}", mpp->get_inst_name()));
    }

    cv::Mat_<cv::Vec4b> img_info(info_height, width, cv::Vec4b(200, 200, 200, 255));
    for (int i = 0; i < msgs.size(); i++) {
      cv::putText(img_info, msgs[i], cv::Point(5, (i + 1) * 25), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0, 0), 1, cv::LINE_AA);
    }
    return img_info;
  }

  cv::Mat_<cv::Vec4b> create_image() {

    auto img_frame = create_board_image();
    const int width_frame = img_frame.cols;
    const int height_frame = img_frame.rows;

    auto img_info = create_info_image();
    const int width_info = img_info.cols;
    const int height_info = img_info.rows;

    assert(width_frame == width_info);

    cv::Mat_<cv::Vec4b> img(height_info + height_frame, width_frame);
    img_info.copyTo(img(cv::Rect(0, 0, width_info, height_info)));
    img_frame.copyTo(img(cv::Rect(0, height_info, width_frame, height_frame)));

    return img;
  }

  void vis() {

    cv::namedWindow(winname, cv::WINDOW_AUTOSIZE);
    img = create_image();
    cv::imshow(winname, img);

    cv::createTrackbar("frame id", winname, &frame_id, canvas_list.size() - 1, frame_callback, this);
    cv::setTrackbarPos("frame id", winname, canvas_list.size() - 1);
    cv::setTrackbarMin("frame id", winname, 0);

    cv::createTrackbar("alpha", winname, &alpha, 255, alpha_callback, this);
    cv::setTrackbarPos("alpha", winname, 0);
    cv::setTrackbarMin("alpha", winname, 0);

    cv::setMouseCallback(winname, mouse_callback, this);

    while (true) {
      int c = cv::waitKey(15);
      if (c == 'b') draw_border_flag = !draw_border_flag;
      if (c == 'h') heatmap_flag = !heatmap_flag;
      if (c == 27 /*ESC*/) break;
      if (c == 2 /*left*/ || c == 3 /*right*/) {
        const int min = 0;
        const int max = canvas_list.size() - 1;
        frame_id = std::clamp(frame_id + (c == 2 ? -1 : 1), min, max);
        cv::setTrackbarPos("frame id", winname, frame_id);
      }

      if (manual) {
        if ('0' <= c && c <= '6') {
          mpp->inst_type = c;
        }
      }

      img = create_image();
      cv::imshow(winname, img);
    }
  }

  std::shared_ptr<Block> get_block(CanvasPtr canvas, const Point& p) {
    std::shared_ptr<Block> selected_block = nullptr;
    for (const auto& [block_id, block] : canvas->blocks) {
      assert(block);
      if (p.isInside(block->bottomLeft, block->topRight)) {
        selected_block = block;
      }
    }
    return selected_block;
  }

  void exec_color_inst(bool draw_optimal) {
    auto point = get_logical_mouse_position();
    auto canvas = canvas_list[frame_id];
    auto block = get_block(canvas, point);
    if (!block) return;
    std::shared_ptr<Instruction> color_inst;
    if (draw_optimal) {
      auto opt_color = *geometricMedianColor(*painting, block->bottomLeft, block->topRight, true);
      // 最適化済みならスルー
      std::string prev_color;
      if (auto sblock = std::dynamic_pointer_cast<SimpleBlock>(block)) {
        prev_color = std::to_string(sblock->color);
        if (sblock->color == opt_color) {
          LOG(INFO) << "block color is optimal!";
          return;
        }
      }
      else {
        prev_color = "complex";
      }
      LOG(INFO) << fmt::format("color change: {} -> {}", prev_color, std::to_string(opt_color));
      color_inst = std::make_shared<ColorInstruction>(block->id, opt_color);
    }
    else {
      auto color = (*painting)(point.px, point.py);
      color_inst = std::make_shared<ColorInstruction>(block->id, color);
    }
    remove_successor_states();
    read_instruction(color_inst);
    fit_frame_trackbar(true);
  }

  void exec_point_cut_inst() {
    auto point = get_logical_mouse_position();
    auto canvas = canvas_list[frame_id];
    auto block = get_block(canvas, point);
    if (!block) return;
    if (!point.isStrictlyInside(block->bottomLeft, block->topRight)) return;
    auto point_inst = std::make_shared<PointCutInstruction>(block->id, point);
    remove_successor_states();
    read_instruction(point_inst);
    fit_frame_trackbar(true);
  }

  void exec_vertical_cut_inst() {
    auto point = get_logical_mouse_position();
    auto canvas = canvas_list[frame_id];
    auto block = get_block(canvas, point);
    if (!block) return;
    if (!point.isStrictlyInside(block->bottomLeft, block->topRight)) return;
    auto vertical_inst = std::make_shared<VerticalCutInstruction>(block->id, point.px);
    remove_successor_states();
    read_instruction(vertical_inst);
    fit_frame_trackbar(true);
  }

  void exec_horizontal_cut_inst() {
    auto point = get_logical_mouse_position();
    auto canvas = canvas_list[frame_id];
    auto block = get_block(canvas, point);
    if (!block) return;
    if (!point.isStrictlyInside(block->bottomLeft, block->topRight)) return;
    auto horizontal_inst = std::make_shared<HorizontalCutInstruction>(block->id, point.py);
    remove_successor_states();
    read_instruction(horizontal_inst);
    fit_frame_trackbar(true);
  }

  void exec_swap_inst() {
    if (mpp->selected_block_id.empty()) {
      auto point = get_logical_mouse_position();
      auto canvas = canvas_list[frame_id];
      auto block = get_block(canvas, point);
      if (!block) return;
      mpp->selected_frame_id = frame_id;
      mpp->selected_block_id = block->id;
    }
    else {
      if (mpp->inst_type != '5' || mpp->selected_frame_id != frame_id) {
        LOG(INFO) << "invalid operation";
        mpp->inst_type = '0';
        mpp->selected_block_id = "";
        mpp->selected_frame_id = -1;
        return;
      }
      auto point = get_logical_mouse_position();
      auto canvas = canvas_list[frame_id];
      auto block2 = get_block(canvas, point);
      if (!block2 || mpp->selected_block_id == block2->id) return;
      auto block1 = canvas->blocks[mpp->selected_block_id];
      int dx1 = block1->topRight.px - block1->bottomLeft.px, dy1 = block1->topRight.py - block1->bottomLeft.py;
      int dx2 = block2->topRight.px - block2->bottomLeft.px, dy2 = block2->topRight.py - block2->bottomLeft.py;
      if (dx1 != dx2 || dy1 != dy2) {
        LOG(INFO) << "invalid block size";
        return;
      }
      auto swap_inst = std::make_shared<SwapInstruction>(mpp->selected_block_id, block2->id);
      remove_successor_states();
      read_instruction(swap_inst);
      fit_frame_trackbar(true);
      mpp->selected_block_id = "";
      mpp->selected_frame_id = -1;
    }
  }

  void exec_merge_inst() {
    if (mpp->selected_block_id.empty()) {
      auto point = get_logical_mouse_position();
      auto canvas = canvas_list[frame_id];
      auto block = get_block(canvas, point);
      if (!block) return;
      mpp->selected_frame_id = frame_id;
      mpp->selected_block_id = block->id;
    }
    else {
      if (mpp->inst_type != '6' || mpp->selected_frame_id != frame_id) {
        LOG(INFO) << "invalid operation";
        mpp->inst_type = '0';
        mpp->selected_block_id = "";
        mpp->selected_frame_id = -1;
        return;
      }
      auto point = get_logical_mouse_position();
      auto canvas = canvas_list[frame_id];
      auto block2 = get_block(canvas, point);
      if (!block2 || mpp->selected_block_id == block2->id) return;
      auto block1 = canvas->blocks[mpp->selected_block_id];

      const auto bottomToTop =
        (block1->bottomLeft.py == block2->topRight.py || block1->topRight.py == block2->bottomLeft.py) &&
        block1->bottomLeft.px == block2->bottomLeft.px &&
        block1->topRight.px == block2->topRight.px;

      const auto leftToRight =
        (block1->bottomLeft.px == block2->topRight.px || block1->topRight.px == block2->bottomLeft.px) &&
        block1->bottomLeft.py == block2->bottomLeft.py &&
        block1->topRight.py == block2->topRight.py;

      if (!bottomToTop && !leftToRight) {
        LOG(INFO) << "invalid block size";
        return;
      }
      auto merge_inst = std::make_shared<MergeInstruction>(mpp->selected_block_id, block2->id);
      remove_successor_states();
      read_instruction(merge_inst);
      fit_frame_trackbar(true);
      mpp->selected_block_id = "";
      mpp->selected_frame_id = -1;
    }
  }

  void exec_mouse_action() {
    bool clicked_left = mp->clicked_left();
    bool clicked_right = mp->clicked_right();
    if (clicked_left || clicked_right) {
      LOG(INFO) << fmt::format("logical mouse position: x={:3d}, y={:3d}", get_logical_mouse_position().px, get_logical_mouse_position().py);

      switch (mpp->inst_type) {
      case '0':
        break;
      case '1': // Color
        exec_color_inst(clicked_left); // draw optimal color
        break;
      case '2': // Point
        exec_point_cut_inst();
        break;
      case '3': // Vertical
        exec_vertical_cut_inst();
        break;
      case '4': // Horizontal
        exec_horizontal_cut_inst();
        break;
      case '5': // Swap TODO: 適当すぎ
        exec_swap_inst();
        break;
      case '6': // Merge
        exec_merge_inst();
        break;
      default:
        break;
      }

    }
  }

  static void frame_callback(int id, void* param) {
    auto vis = static_cast<SeekBarVisualizer*>(param);
    vis->frame_id = id;
  }

  static void alpha_callback(int val, void* param) {
    auto vis = static_cast<SeekBarVisualizer*>(param);
    vis->alpha = val;
  }

  static void mouse_callback(int e, int x, int y, int f, void* param) {
    auto vis = static_cast<SeekBarVisualizer*>(param);
    vis->mp->load(e, x, y, f);
    vis->exec_mouse_action();
  }

};



class Visualizer : public SolverBase {
public:

  struct Option : public OptionBase {
    bool manual = false;
    void setOptionParser(CLI::App* app) override {
      app->add_flag("--manual", manual);
    }
  };
  virtual OptionBase::Ptr createOption() { return std::make_shared<Option>(); }

  Visualizer() { }
  SolverOutputs solve(const SolverArguments &args) override {

    SolverOutputs ret;

    bool manual = getOption<Option>()->manual;
    LOG(INFO) << fmt::format("manual mode: {}", manual);

    auto canvas = args.initial_canvas->Clone();
    SeekBarVisualizer visualizer(args.painting, canvas, manual);

    auto instructions = args.optional_initial_solution;
    for (const auto& inst : instructions) {
      visualizer.read_instruction(inst);
    }

    visualizer.vis();

    if (manual) {
      ret.solution = visualizer.instructions;
    }
    else {
      ret.solution = args.optional_initial_solution;
    }

    return ret;

  }
};
REGISTER_SOLVER_WITH_OPTION("Visualizer", Visualizer, Visualizer::Option);

// vim:ts=2 sw=2 sts=2 et ci

