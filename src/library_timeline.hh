#pragma once

#include <cmath>
#include <memory>

#include "animation.hh"
#include "base.hh"
#include "gui_button.hh"
#include "on_off.hh"
#include "run_button.hh"
#include "time.hh"
#include "timer_thread.hh"

namespace automat::library {

struct SideButton : gui::Button {
  using gui::Button::Button;
  SkColor ForegroundColor(gui::DrawContext&) const override;
  SkColor BackgroundColor() const override;
  SkRRect RRect() const override;
};

struct PrevButton : SideButton {
  PrevButton();
  void Activate(gui::Pointer&) override;
};

struct NextButton : SideButton {
  NextButton();
  void Activate(gui::Pointer&) override;
};

struct Timeline;

struct TimelineRunButton : gui::ToggleButton {
  Timeline* timeline;

  std::unique_ptr<gui::Button> rec_button;
  mutable gui::Button* last_on_widget = nullptr;

  TimelineRunButton(Timeline* timeline);
  gui::Button* OnWidget() const override;
  bool Filled() const override;
  void Activate(gui::Pointer&);
};

struct TrackBase : Object {
  Timeline* timeline = nullptr;
  maf::Vec<time::T> timestamps;
  SkPath Shape(animation::Display*) const override;
  animation::Phase Draw(gui::DrawContext&) const override;
  std::unique_ptr<Action> ButtonDownAction(gui::Pointer&, gui::PointerButton) override;
  virtual void UpdateOutput(Location& target, time::SteadyPoint started_at,
                            time::SteadyPoint now) = 0;

  void SerializeState(Serializer& writer, const char* key) const override;
  void DeserializeState(Location& l, Deserializer& d) override;
  virtual bool TryDeserializeField(Location& l, Deserializer& d, maf::Str& field_name);
};

struct OnOffTrack : TrackBase, OnOff {
  time::T on_at = NAN;
  string_view Name() const override { return "On/Off Track"; }
  std::unique_ptr<Object> Clone() const override { return std::make_unique<OnOffTrack>(*this); }
  animation::Phase Draw(gui::DrawContext&) const override;
  void UpdateOutput(Location& target, time::SteadyPoint started_at, time::SteadyPoint now) override;

  bool IsOn() const override;
  void On() override {}
  void Off() override {}

  void SerializeState(Serializer& writer, const char* key) const override;
  void DeserializeState(Location& l, Deserializer& d) override;
  bool TryDeserializeField(Location& l, Deserializer& d, maf::Str& field_name) override;
};

// Currently Timeline pauses at end which is consistent with standard media player behavour.
// This is fine for MVP but in the future, timeline should keep playing (stuck at the end).
// The user should be able to connect the "next" connection to the "jump to start" so that it loops
// (or stops).
struct Timeline : LiveObject, Runnable, LongRunning, TimerNotificationReceiver {
  static const Timeline proto;

  TimelineRunButton run_button;
  PrevButton prev_button;
  NextButton next_button;

  maf::Vec<std::unique_ptr<TrackBase>> tracks;
  maf::Vec<std::unique_ptr<Argument>> track_args;

  mutable animation::Approach<> zoom;  // stores the time in seconds

  enum State { kPaused, kPlaying, kRecording } state;
  time::T timeline_length = 0;

  struct Paused {
    time::T playback_offset;  // Used when playback is paused
  };

  struct Playing {
    time::SteadyPoint started_at;  // Used when playback is active
  };

  struct Recording {
    time::SteadyPoint started_at;  // Used when recording is active
    // there is no point in staring the length of the timeline because it's always `now -
    // started_at`
  };

  union {
    Paused paused;
    Playing playing;
    Recording recording;
  };

  Timeline();
  Timeline(const Timeline&);
  string_view Name() const override;
  std::unique_ptr<Object> Clone() const override;
  animation::Phase Draw(gui::DrawContext&) const override;
  SkPath Shape(animation::Display*) const override;
  void Args(std::function<void(Argument&)> cb) override;
  Vec2AndDir ArgStart(const Argument&) override;
  ControlFlow VisitChildren(gui::Visitor& visitor) override;
  SkMatrix TransformToChild(const Widget& child, animation::Display*) const override;
  std::unique_ptr<Action> ButtonDownAction(gui::Pointer&, gui::PointerButton) override;
  LongRunning* OnRun(Location& here) override;
  void Cancel() override;
  void OnTimerNotification(Location&, time::SteadyPoint) override;
  OnOffTrack& AddOnOffTrack(maf::StrView name);

  void BeginRecording();
  void StopRecording();

  time::T MaxTrackLength() const;

  void SerializeState(Serializer& writer, const char* key) const override;
  void DeserializeState(Location& l, Deserializer& d) override;
};

}  // namespace automat::library