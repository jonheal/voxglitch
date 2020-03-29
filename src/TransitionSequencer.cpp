//
// Voxglitch "Transition Sequencer" module for VCV Rack
// by Bret Truchan
//
// TODO: When dragging points, have 'crosshairs' show up that extend to the
// x/y legends.
// TODO: When hovering under or above a point, create a vertical line at that
//  point.  If clicking in this state, reposition point up or down.
// TODO: shift-click on existing point should remove it

#include "plugin.hpp"
#include "osdialog.h"
#include "settings.hpp"
#include <fstream>
#include <array>

#define NUMBER_OF_SEQUENCERS 6
#define MAX_SEQUENCER_STEPS 32
#define NUMBER_OF_VOLTAGE_RANGES 8
#define NUMBER_OF_SNAP_DIVISIONS 8

// Constants for patterns
#define DRAW_AREA_WIDTH 486.0
#define DRAW_AREA_HEIGHT 206.0
#define BAR_HEIGHT 214.0
#define BAR_HORIZONTAL_PADDING .8
#define DRAW_AREA_POSITION_X 9
#define DRAW_AREA_POSITION_Y 9.5

#define MINI_MAP_POSITION_X 5.5
#define MINI_MAP_POSITION_Y 84.3
#define MINI_MAP_DRAW_AREA_WIDTH 506.0
#define MINI_MAP_DRAW_AREA_HEIGHT 32.1
#define MINI_MAP_MULTIPLIER 1

#define TOOLTIP_WIDTH 33.0
#define TOOLTIP_HEIGHT 20.0

/*
double voltage_ranges[NUMBER_OF_VOLTAGE_RANGES][2] = {
{ 0.0, 10.0 },
{ -10.0, 10.0 },
{ 0.0, 5.0 },
{ -5.0, 5.0 },
{ 0.0, 3.0 },
{ -3.0, 3.0 },
{ 0.0, 1.0},
{ -1.0, 1.0}
};
*/

struct TimelineSequencerViewport
{
  float width = DRAW_AREA_WIDTH;
  float height = DRAW_AREA_HEIGHT;
  float offset = 0;
};

struct TimelineSequencer
{
  // "points" is a map with the keys representing milliseconds and the value
  // representing the VC value at that position in time.
  // std::map<double, float> points;
  std::vector<Vec> points;

  TimelineSequencerViewport viewport;

  // constructor
  TimelineSequencer()
  {
    points.push_back(Vec(100, 100.50));
    points.push_back(Vec(220, 120.00));
    points.push_back(Vec(300, 60.00));
  }

  Vec getPoint(unsigned int index)
  {
    return(Vec(points[index].x, points[index].y));
  }

  Vec getPointPositionRelativeToViewport(unsigned int index)
  {
    return(Vec(points[index].x - viewport.offset, points[index].y));
  }

  Vec viewportFromIndex(Vec position)
  {
    return(Vec(position.x - viewport.offset, position.y));
  }

  float viewportFromIndex(float x)
  {
    return(x - viewport.offset);
  }

  Vec indexFromViewport(Vec position)
  {
    return(Vec(viewport.offset + position.x, position.y));
  }

  float indexFromViewport(float x)
  {
    return(x + viewport.offset);
  }

  void removePoint(unsigned int index)
  {
    points.erase(points.begin() + index);
  }

  void setViewpointOffset(float offset)
  {
    if(offset > 0) viewport.offset = offset;
  }

  float getViewpointOffset()
  {
    return(viewport.offset);
  }

  std::pair<unsigned int, unsigned int> getPointIndexesWithinViewport()
  {
    unsigned int begin_index = 0;
    unsigned int end_index = 0;

    // I'm not claiming that my algorithm is the best, but do take notice
    // that you can't say: if(begin_index == 0) begin_index = i; because
    // begin_index will be 1 in the case that the first point is at index 0.
    // This threw me for a while, so I thought I'd mention it.

    bool begin_index_located = false;

    for(std::size_t i=0; i < points.size(); i++)
    {
      if((points[i].x >= viewport.offset) && (points[i].x <= (viewport.offset + viewport.width)))
      {
        if(! begin_index_located)
        {
          begin_index = i;
          begin_index_located = true;
        }
        end_index = i;
      }
    }
    return { begin_index, end_index };
  }
};

struct TransitionSequencer : Module
{
  int selected_sequencer_index = 0;
  double sample_rate;
  TimelineSequencer sequencer;

  dsp::SchmittTrigger sequencer_1_button_trigger;
  dsp::SchmittTrigger sequencer_2_button_trigger;
  dsp::SchmittTrigger sequencer_3_button_trigger;
  dsp::SchmittTrigger sequencer_4_button_trigger;
  dsp::SchmittTrigger sequencer_5_button_trigger;
  dsp::SchmittTrigger sequencer_6_button_trigger;

  bool sequencer_1_button_is_triggered;
  bool sequencer_2_button_is_triggered;
  bool sequencer_3_button_is_triggered;
  bool sequencer_4_button_is_triggered;
  bool sequencer_5_button_is_triggered;
  bool sequencer_6_button_is_triggered;

  int voltage_outputs[NUMBER_OF_SEQUENCERS];

  std::string voltage_range_names[NUMBER_OF_VOLTAGE_RANGES] = {
    "0.0 to 10.0",
    "-10.0 to 10.0",
    "0.0 to 5.0",
    "-5.0 to 5.0",
    "0.0 to 3.0",
    "-3.0 to 3.0",
    "0.0 to 1.0",
    "-1.0 to 1.0"
  };

  enum ParamIds {
    SEQUENCER_1_BUTTON,
    SEQUENCER_2_BUTTON,
    SEQUENCER_3_BUTTON,
    SEQUENCER_4_BUTTON,
    SEQUENCER_5_BUTTON,
    SEQUENCER_6_BUTTON,
    NUM_PARAMS
  };
  enum InputIds {
    RESET_INPUT,
    NUM_INPUTS
  };
  enum OutputIds {
    SEQ1_CV_OUTPUT,
    SEQ2_CV_OUTPUT,
    SEQ3_CV_OUTPUT,
    SEQ4_CV_OUTPUT,
    SEQ5_CV_OUTPUT,
    SEQ6_CV_OUTPUT,

    NUM_OUTPUTS
  };
  enum LightIds {
    SEQUENCER_1_LIGHT,
    SEQUENCER_2_LIGHT,
    SEQUENCER_3_LIGHT,
    SEQUENCER_4_LIGHT,
    SEQUENCER_5_LIGHT,
    SEQUENCER_6_LIGHT,
    NUM_LIGHTS
  };

  //
  // Constructor
  //
  TransitionSequencer()
  {
    voltage_outputs[0] = SEQ1_CV_OUTPUT;
    voltage_outputs[1] = SEQ2_CV_OUTPUT;
    voltage_outputs[2] = SEQ3_CV_OUTPUT;
    voltage_outputs[3] = SEQ4_CV_OUTPUT;
    voltage_outputs[4] = SEQ5_CV_OUTPUT;
    voltage_outputs[5] = SEQ6_CV_OUTPUT;

    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
    configParam(SEQUENCER_1_BUTTON, 0.f, 1.f, 0.f, "Sequence1Button");
    configParam(SEQUENCER_2_BUTTON, 0.f, 1.f, 0.f, "Sequence2Button");
    configParam(SEQUENCER_3_BUTTON, 0.f, 1.f, 0.f, "Sequence3Button");
    configParam(SEQUENCER_4_BUTTON, 0.f, 1.f, 0.f, "Sequence4Button");
    configParam(SEQUENCER_5_BUTTON, 0.f, 1.f, 0.f, "Sequence5Button");
    configParam(SEQUENCER_6_BUTTON, 0.f, 1.f, 0.f, "Sequence6Button");
  }

  /*
  ==================================================================================================================================================
  ___                 _
  / / |               | |
  ___  __ ___   _____  / /| | ___   __ _  __| |
  / __|/ _` \ \ / / _ \ / / | |/ _ \ / _` |/ _` |
  \__ \ (_| |\ V /  __// /  | | (_) | (_| | (_| |
  |___/\__,_| \_/ \___/_/   |_|\___/ \__,_|\__,_|

  ==================================================================================================================================================
  */

  json_t *dataToJson() override
  {
    json_t *json_root = json_object();
    return json_root;
  }

  // Autoload settings
  void dataFromJson(json_t *json_root) override
  {
  }


  /*

  ______
  | ___ \
  | |_/ / __ ___   ___ ___  ___ ___
  |  __/ '__/ _ \ / __/ _ \/ __/ __|
  | |  | | | (_) | (_|  __/\__ \__ \
  \_|  |_|  \___/ \___\___||___/___/


  */

  void process(const ProcessArgs &args) override
  {
    this->sample_rate = args.sampleRate;

    sequencer_1_button_is_triggered = sequencer_1_button_trigger.process(params[SEQUENCER_1_BUTTON].getValue());
    sequencer_2_button_is_triggered = sequencer_2_button_trigger.process(params[SEQUENCER_2_BUTTON].getValue());
    sequencer_3_button_is_triggered = sequencer_3_button_trigger.process(params[SEQUENCER_3_BUTTON].getValue());
    sequencer_4_button_is_triggered = sequencer_4_button_trigger.process(params[SEQUENCER_4_BUTTON].getValue());
    sequencer_5_button_is_triggered = sequencer_5_button_trigger.process(params[SEQUENCER_5_BUTTON].getValue());
    sequencer_6_button_is_triggered = sequencer_6_button_trigger.process(params[SEQUENCER_6_BUTTON].getValue());

    if(sequencer_1_button_is_triggered) selected_sequencer_index = 0;
    if(sequencer_2_button_is_triggered) selected_sequencer_index = 1;
    if(sequencer_3_button_is_triggered) selected_sequencer_index = 2;
    if(sequencer_4_button_is_triggered) selected_sequencer_index = 3;
    if(sequencer_5_button_is_triggered) selected_sequencer_index = 4;
    if(sequencer_6_button_is_triggered) selected_sequencer_index = 5;


    lights[SEQUENCER_1_LIGHT].setBrightness(selected_sequencer_index == 0);
    lights[SEQUENCER_2_LIGHT].setBrightness(selected_sequencer_index == 1);
    lights[SEQUENCER_3_LIGHT].setBrightness(selected_sequencer_index == 2);
    lights[SEQUENCER_4_LIGHT].setBrightness(selected_sequencer_index == 3);
    lights[SEQUENCER_5_LIGHT].setBrightness(selected_sequencer_index == 4);
    lights[SEQUENCER_6_LIGHT].setBrightness(selected_sequencer_index == 5);
  }

};

/*

_    _ _     _            _
| |  | (_)   | |          | |
| |  | |_  __| | __ _  ___| |_ ___
| |/\| | |/ _` |/ _` |/ _ \ __/ __|
\  /\  / | (_| | (_| |  __/ |_\__ \
\/  \/|_|\__,_|\__, |\___|\__|___/
__/ |
|___/
*/

struct TimelineSequencerWidget : TransparentWidget
{
  TransitionSequencer *module;
  Vec drag_position;
  unsigned int selected_point_index = 0;
  bool dragging_point = false;
  unsigned int hover_point_index = 0;
  bool hovering_over_point = true;

  TimelineSequencerWidget()
  {
    box.size = Vec(DRAW_AREA_WIDTH, DRAW_AREA_HEIGHT);
  }

  void draw(const DrawArgs &args) override
  {
    const auto vg = args.vg;

    // Save the drawing context to restore later
    nvgSave(vg);

    if(module)
    {
      // for each point
      //   if point is in window
      //     draw point
      //     draw line from previous point to this point
      // end foreach
      // draw line from last visible point to point offscreen

      float previous_x = -1;
      float previous_y = -1;

      unsigned int start_index;
      unsigned int end_index;

      std::tie(start_index, end_index) = module->sequencer.getPointIndexesWithinViewport();

      // if(start_index > 0) start_index--;

      //
      // Draw all the lines first
      //

      if (start_index > 0) draw_line_offscreen_left(vg, start_index);
      if (end_index < (module->sequencer.points.size() - 1)) draw_line_offscreen_right(vg, end_index);

      for(std::size_t i=start_index; i <= end_index; i++)
      {
        Vec position = module->sequencer.getPointPositionRelativeToViewport(i);

        /*
        // for (std::pair<std::double, float> element : module->sequencer.points) {
        // Accessing KEY from element
        float time = module->sequencer.points[i].x;
        float cv_value = module->sequencer.points[i].y;

        // calculate position based on time and the position of the
        // drawing window.
        float draw_position_x = time;  // TODO: finish this
        float draw_position_y = cv_value;
        */

        if(previous_x >= 0)
        {
          nvgBeginPath(vg);
          nvgMoveTo(vg, previous_x, previous_y);
          nvgLineTo(vg, position.x, position.y);
          nvgStrokeColor(vg, nvgRGBA(156, 167, 185, 255));
          nvgStroke(vg);
        }

        previous_x = position.x;
        previous_y = position.y;
      }

      for(std::size_t i=start_index; i <= end_index; i++)
      {
        // for (std::pair<std::double, float> element : module->sequencer.points) {
        // Accessing KEY from element

        Vec position = module->sequencer.getPointPositionRelativeToViewport(i);

        // (outer circle)
        nvgBeginPath(vg);
        nvgCircle(vg, position.x, position.y, 10);
        nvgFillColor(vg, nvgRGBA(156, 167, 185, 20));
        nvgFill(vg);

        bool highlight_point = false;
        if(dragging_point && selected_point_index == i) highlight_point = true;
        if(hovering_over_point && hover_point_index == i) highlight_point = true;

        // (inner circle)
        nvgBeginPath(vg);
        nvgCircle(vg, position.x, position.y, 5);
        nvgFillColor(vg, nvgRGBA(156, 167, 185, 255));
        if(highlight_point) nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
        nvgFill(vg);
      }

      // testing draw area
      /*
      nvgBeginPath(vg);
      nvgRect(vg, 0, 0, DRAW_AREA_WIDTH, DRAW_AREA_HEIGHT);
      nvgFillColor(vg, nvgRGBA(120, 20, 20, 100));
      nvgFill(vg);
      */
    }

    nvgRestore(vg);
  }

  void draw_line_offscreen_left(NVGcontext *vg, float start_index)
  {
    // draw line from left side of the screen to the first point
    Vec position = module->sequencer.getPointPositionRelativeToViewport(start_index);
    Vec previous_position =  module->sequencer.getPointPositionRelativeToViewport(start_index-1);

    float x2 = position.x;
    float y2 = position.y;
    float x1 = previous_position.x;
    float y1 = previous_position.y;

    float m = (y2 - y1) / (x2 - x1);
    float yIntercept = y1 - (m * x1);

    nvgBeginPath(vg);
    nvgMoveTo(vg, position.x, position.y);
    nvgLineTo(vg, 0, yIntercept);
    nvgStrokeColor(vg, nvgRGBA(156, 167, 185, 255));
    nvgStroke(vg);
  }


  void draw_line_offscreen_right(NVGcontext *vg, float end_index)
  {
    // draw line from left side of the screen to the first point
    //     Vec position = module->sequencer.getPointPositionRelativeToViewport(end_index);
    //    Vec next_position =  module->sequencer.getPointPositionRelativeToViewport(end_index + 1);

    Vec position = module->sequencer.getPointPositionRelativeToViewport(end_index);
    Vec next_position =  module->sequencer.getPointPositionRelativeToViewport(end_index + 1);

    float x2 = position.x;
    float y2 = position.y;
    float x1 = next_position.x;
    float y1 = next_position.y;

    float m = (y2 - y1) / (x2 - x1);
    float yIntercept = y1 - (m * x1);

    nvgBeginPath(vg);
    nvgMoveTo(vg, position.x, position.y);
    nvgLineTo(vg, DRAW_AREA_WIDTH, yIntercept);
    nvgStrokeColor(vg, nvgRGBA(156, 167, 185, 255));
    nvgStroke(vg);
  }

  void onButton(const event::Button &e) override
  {
    if(e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
    {
      e.consume(this);
      drag_position = e.pos;

      if(e.mods == GLFW_MOD_SHIFT)
      {

        //
        // If the user is mousing over a point while shift-clicking,
        // the point will be removed
        //
        bool point_is_moused_over = false;
        unsigned int point_index = 0;

        std::tie(point_is_moused_over, point_index) = mousedOverPoint(e.pos);

        if(point_is_moused_over)
        {
          // Remove point
          //...
          module->sequencer.removePoint(point_index);
        }
        else
        {
          // Insert new point
          // TODO: replace drag_position in the followin line of code
          // with the position computed given the draw window

          unsigned int number_of_points = module->sequencer.points.size();
          int insert_at_location = 0;



          if(number_of_points == 0)
          {
            insert_at_location = 0;
          }
          // If adding a point to the beginning
          else if(drag_position.x < module->sequencer.viewportFromIndex(module->sequencer.points[0].x))
          {
            insert_at_location = 0;
          }
          // If adding a point to the end
          else if(drag_position.x > module->sequencer.viewportFromIndex(module->sequencer.points.back().x))
          {
            insert_at_location = module->sequencer.points.size();
          }

          // If adding a point somewhere in the middle
          else
          {
            for(std::size_t i=0; i<module->sequencer.points.size() - 1; i++)
            {
              float first_point_viewport_x = module->sequencer.viewportFromIndex(module->sequencer.points[i].x);
              float next_point_viewport_x = module->sequencer.viewportFromIndex(module->sequencer.points[i+1].x);

              if((drag_position.x > first_point_viewport_x) && (drag_position.x < next_point_viewport_x))
              {
                insert_at_location = i + 1;
              }
            }
          }


          // This cannot be done while iterating over the vector
          module->sequencer.points.insert(module->sequencer.points.begin() + insert_at_location, drag_position);

          // Begin dragging
          selected_point_index = insert_at_location;
          dragging_point = true;
        }
      }
      else
      {
        // see if the user is selecting a point
        std::tie(dragging_point, selected_point_index) = mousedOverPoint(e.pos);
      }
    }

    if(e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_RELEASE)
    {
      dragging_point = false;
    }
  }

  void onDragMove(const event::DragMove &e) override
  {
    TransparentWidget::onDragMove(e);
    double zoom = std::pow(2.f, settings::zoom);
    drag_position = drag_position.plus(e.mouseDelta.div(zoom));

    // IF the user is dragging a point
    if(dragging_point && module->sequencer.points.size() > 0)
    {
      Vec point_position = module->sequencer.indexFromViewport(drag_position);

      if(selected_point_index > 0 )
      {
        if (point_position.x > module->sequencer.points[selected_point_index - 1].x)
        {
          module->sequencer.points[selected_point_index] = point_position;
        }
        else
        {
          module->sequencer.points[selected_point_index].x = module->sequencer.points[selected_point_index - 1].x;
        }
      }
      else
      {
        Vec point_position = module->sequencer.indexFromViewport(drag_position);
        module->sequencer.points[selected_point_index] = point_position;
      }
    }
    // Otherwise let the mouse drag the viewport offset
    else
    {
      float drag_x = e.mouseDelta.div(zoom).x;
      module->sequencer.setViewpointOffset(module->sequencer.getViewpointOffset() - drag_x);
    }
  }

  void onLeave(const event::Leave &e) override
  {
    TransparentWidget::onLeave(e);
  }

  void onHover(const event::Hover& e) override
  {
    TransparentWidget::onHover(e);
    e.consume(this);

    hovering_over_point = false;
    std::tie(hovering_over_point, hover_point_index) = mousedOverPoint(e.pos);
  }

  std::pair<bool, unsigned int> mousedOverPoint(Vec mouse_position)
  {
    unsigned int point_index = 0;
    bool mouse_is_over_point = false;

    // TODO: change this to only iterate over visible points
    for(std::size_t i=0; i<module->sequencer.points.size(); i++)
    {
      Vec position = module->sequencer.viewportFromIndex(module->sequencer.points[i]);

      if(position.x > (mouse_position.x - 16) &&
      position.x < (mouse_position.x + 16) &&
      position.y > (mouse_position.y - 16) &&
      position.y < (mouse_position.y + 16)
    )
    {
      point_index = i;
      mouse_is_over_point = true;
    }
  }

  return { mouse_is_over_point, point_index };
}
};

struct TimelineMiniMapWidget : TransparentWidget
{
  TransitionSequencer *module;
  Vec drag_position;
  float window_box_position;
  float window_box_width = 32;

  TimelineMiniMapWidget()
  {
    box.size = Vec(MINI_MAP_DRAW_AREA_WIDTH, MINI_MAP_DRAW_AREA_HEIGHT);
    window_box_position = 0;
  }

  void draw(const DrawArgs &args) override
  {
    const auto vg = args.vg;

    // Save the drawing context to restore later
    nvgSave(vg);

    if(module)
    {
      // testing draw area
      /*
      nvgBeginPath(vg);
      nvgRect(vg, 0, 0, MINI_MAP_DRAW_AREA_WIDTH, MINI_MAP_DRAW_AREA_HEIGHT);
      nvgFillColor(vg, nvgRGBA(120, 120, 20, 40));
      nvgFill(vg);
      */

      // window_box_width = (MINI_MAP_DRAW_AREA_WIDTH / (module->sequencer.points.back().x + MINI_MAP_DRAW_AREA_WIDTH)) * MINI_MAP_DRAW_AREA_WIDTH;
      // DEBUG(std::to_string(window_box_width).c_str());

      // draw window box
      nvgBeginPath(vg);
      nvgRoundedRect(vg, window_box_position, 0, window_box_width, MINI_MAP_DRAW_AREA_HEIGHT, 3);
      nvgFillColor(vg, nvgRGBA(100, 100, 100, 150));
      nvgFill(vg);
    }

    nvgRestore(vg);
  }

  void onButton(const event::Button &e) override
  {
    if(e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
    {
      e.consume(this);
      drag_position = e.pos;
      this->reposition(e.pos.x);
    }
  }

  void onDragMove(const event::DragMove &e) override
  {
    TransparentWidget::onDragMove(e);
    double zoom = std::pow(2.f, settings::zoom);
    drag_position = drag_position.plus(e.mouseDelta.div(zoom));
    this->reposition(drag_position.x);
  }

  void reposition(float x)
  {
    // DEBUG(std::to_string(x).c_str());
    float centered_position = x - 16;
    if(centered_position < 0) centered_position = 0;
    if(centered_position > 472.76) centered_position = 472.76;
    // DEBUG(std::to_string(centered_position).c_str());

    module->sequencer.setViewpointOffset(centered_position * MINI_MAP_MULTIPLIER);
    window_box_position = centered_position;
  }
};

struct TransitionSequencerWidget : ModuleWidget
{
  TransitionSequencer* module;

  TransitionSequencerWidget(TransitionSequencer* module)
  {
    this->module = module;
    setModule(module);
    setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/transition_sequencer_front_panel.svg")));

    // Cosmetic rack screws
    addChild(createWidget<ScrewSilver>(Vec(15, 0)));
    addChild(createWidget<ScrewSilver>(Vec(15, 365)));
    addChild(createWidget<ScrewSilver>(mm2px(Vec(171.5, 0))));

    double button_spacing = 9.6; // 9.1
    double button_group_x = 48.0;
    double button_group_y = 103.0;
    // Sequence 1 button
    addParam(createParamCentered<LEDButton>(mm2px(Vec(button_group_x, button_group_y)), module, TransitionSequencer::SEQUENCER_1_BUTTON));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(button_group_x, button_group_y)), module, TransitionSequencer::SEQUENCER_1_LIGHT));
    // Sequence 2 button
    addParam(createParamCentered<LEDButton>(mm2px(Vec(button_group_x + (button_spacing * 1.0), button_group_y)), module, TransitionSequencer::SEQUENCER_2_BUTTON));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(button_group_x + (button_spacing * 1.0), button_group_y)), module, TransitionSequencer::SEQUENCER_2_LIGHT));
    // Sequence 3 button
    addParam(createParamCentered<LEDButton>(mm2px(Vec(button_group_x + (button_spacing * 2.0), button_group_y)), module, TransitionSequencer::SEQUENCER_3_BUTTON));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(button_group_x + (button_spacing * 2.0), button_group_y)), module, TransitionSequencer::SEQUENCER_3_LIGHT));
    // Sequence 4 button
    addParam(createParamCentered<LEDButton>(mm2px(Vec(button_group_x + (button_spacing * 3.0), button_group_y)), module, TransitionSequencer::SEQUENCER_4_BUTTON));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(button_group_x + (button_spacing * 3.0), button_group_y)), module, TransitionSequencer::SEQUENCER_4_LIGHT));
    // Sequence 5 button
    addParam(createParamCentered<LEDButton>(mm2px(Vec(button_group_x + (button_spacing * 4.0), button_group_y)), module, TransitionSequencer::SEQUENCER_5_BUTTON));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(button_group_x + (button_spacing * 4.0), button_group_y)), module, TransitionSequencer::SEQUENCER_5_LIGHT));
    // Sequence 6 button
    addParam(createParamCentered<LEDButton>(mm2px(Vec(button_group_x + (button_spacing * 5.0), button_group_y)), module, TransitionSequencer::SEQUENCER_6_BUTTON));
    addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(button_group_x + (button_spacing * 5.0), button_group_y)), module, TransitionSequencer::SEQUENCER_6_LIGHT));

    // 6 sequencer outputs
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(118, 108.224)), module, TransitionSequencer::SEQ1_CV_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(129, 108.224)), module, TransitionSequencer::SEQ2_CV_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(140, 108.224)), module, TransitionSequencer::SEQ3_CV_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(151, 108.224)), module, TransitionSequencer::SEQ4_CV_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(162, 108.224)), module, TransitionSequencer::SEQ5_CV_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(173, 108.224)), module, TransitionSequencer::SEQ6_CV_OUTPUT));

    TimelineSequencerWidget *timeline_sequencer_widget = new TimelineSequencerWidget();
    timeline_sequencer_widget->box.pos = mm2px(Vec(DRAW_AREA_POSITION_X, DRAW_AREA_POSITION_Y));
    timeline_sequencer_widget->module = module;
    addChild(timeline_sequencer_widget);

    TimelineMiniMapWidget *timeline_mini_map_widget = new TimelineMiniMapWidget();
    timeline_mini_map_widget->box.pos = mm2px(Vec(MINI_MAP_POSITION_X, MINI_MAP_POSITION_Y));
    timeline_mini_map_widget->module = module;
    addChild(timeline_mini_map_widget);
  }

  void appendContextMenu(Menu *menu) override
  {
    TransitionSequencer *module = dynamic_cast<TransitionSequencer*>(this->module);
    assert(module);
  }

  void step() override {
    ModuleWidget::step();
  }

};

Model* modelTransitionSequencer = createModel<TransitionSequencer, TransitionSequencerWidget>("transitionsequencer");
