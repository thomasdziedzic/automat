#pragma once

#include <memory>

#include "animation.hh"
#include "base.hh"
#include "gui_button.hh"
#include "on_off.hh"
#include "run_button.hh"
#include "time.hh"

namespace automat::library {

struct PrevButton : virtual gui::Button, gui::ChildButtonMixin, gui::CircularButtonMixin {
  PrevButton();
  void Activate(gui::Pointer&) override;
  SkColor ForegroundColor() const override;
  SkColor BackgroundColor() const override;
};

struct NextButton : virtual gui::Button, gui::ChildButtonMixin, gui::CircularButtonMixin {
  NextButton();
  void Activate(gui::Pointer&) override;
  SkColor ForegroundColor() const override;
  SkColor BackgroundColor() const override;
};

struct TimelineRunButton : virtual gui::Button, gui::RunButton {
  TimelineRunButton();
  SkColor ForegroundColor() const override;
  SkColor BackgroundColor() const override;
};

struct Timeline;

struct TrackBase : Object {
  Timeline* timeline = nullptr;
  Vec<time::T> timestamps;
  SkPath Shape() const override;
  void Draw(gui::DrawContext&) const override;
  std::unique_ptr<Action> ButtonDownAction(gui::Pointer&, gui::PointerButton) override;
  virtual void UpdateOutput(Location& target, time::T current_offset) = 0;
};

struct OnOffTrack : TrackBase, OnOff {
  string_view Name() const override { return "On/Off Track"; }
  std::unique_ptr<Object> Clone() const override { return std::make_unique<OnOffTrack>(*this); }
  void Draw(gui::DrawContext&) const override;
  void UpdateOutput(Location& target, time::T current_offset) override;

  bool IsOn() const override;
  void On() override {}
  void Off() override {}
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

  Vec<std::unique_ptr<TrackBase>> tracks;
  Vec<std::unique_ptr<Argument>> track_args;

  mutable animation::Approach zoom;  // stores the time in seconds

  enum State { kPaused, kPlaying, kRecording } state;

  struct Paused {
    time::T playback_offset;  // Used when playback is paused
  };

  struct Playing {
    time::SteadyPoint playback_started_at;  // Used when playback is active
  };

  struct Recording {
    time::SteadyPoint recording_started_at;  // Used when recording is active
  };

  union {
    Paused paused;
    Playing playing;
    Recording recording;
  };

  Timeline();
  Timeline(const Timeline&);
  void Relocate(Location* new_here) override;
  string_view Name() const override;
  std::unique_ptr<Object> Clone() const override;
  void Draw(gui::DrawContext&) const override;
  SkPath Shape() const override;
  void Args(std::function<void(Argument&)> cb) override;
  Vec2AndDir ArgStart(Argument&) override;
  ControlFlow VisitChildren(gui::Visitor& visitor) override;
  SkMatrix TransformToChild(const Widget& child, animation::Context&) const override;
  std::unique_ptr<Action> ButtonDownAction(gui::Pointer&, gui::PointerButton) override;
  LongRunning* OnRun(Location& here) override;
  void Cancel() override;
  void OnTimerNotification(Location&, time::SteadyPoint) override;
  OnOffTrack& AddOnOffTrack(StrView name);

  time::T MaxTrackLength() const;
};

}  // namespace automat::library