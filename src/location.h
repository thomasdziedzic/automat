#pragma once

#include <memory>
#include <string>

#include "connection.h"
#include "error.h"
#include "gui_connection_widget.h"
#include "object.h"
#include "run_button.h"
#include "string_multimap.h"
#include "tasks.h"
#include "text_field.h"
#include "widget.h"

namespace automat {

struct DragLocationAction;

// Each Container holds its inner objects in Locations.
//
// Location specifies location & relations of an object.
//
// Locations provide common interface for working with Containers of various
// types (2d canvas, 3d space, list, hashmap, etc.). In that sense they are
// similar to C++ iterators.
//
// Implementations of this interface would typically extend it with
// container-specific functions.
struct Location : gui::Widget {
  Location *parent;

  std::unique_ptr<Object> object;

  // Name of this Location.
  std::string name;
  gui::TextField name_text_field;
  gui::RunButton run_button;
  std::vector<std::unique_ptr<gui::ConnectionWidget>> connection_widgets;

  DragLocationAction *drag_action = nullptr;
  vec2 position = {0, 0};

  // Connections of this Location.
  // Connection is owned by both incoming & outgoing locations.
  string_multimap<Connection *> outgoing;
  string_multimap<Connection *> incoming;

  std::unordered_set<Location *> update_observers;
  std::unordered_set<Location *> observing_updates;

  std::unordered_set<Location *> error_observers;
  std::unordered_set<Location *> observing_errors;

  RunTask run_task;

  Location(Location *parent = nullptr);

  Widget *ParentWidget() const override {
    if (parent == nullptr) {
      return nullptr;
    }
    return parent->object.get();
  }

  std::string_view Name() const override { return name; }

  std::unique_ptr<Object> InsertHere(std::unique_ptr<Object> &&object) {
    this->object.swap(object);
    return object;
  }

  Object *Create(const Object &prototype) {
    object = prototype.Clone();
    object->Relocate(this);
    return object.get();
  }

  template <typename T> T *Create() {
    return dynamic_cast<T *>(Create(T::proto));
  }

  // Remove the objects held by this location.
  //
  // Some containers may not allow empty locations so this function may also
  // delete the location. Check the return value.
  Location *Clear() {
    object.reset();
    return this;
  }

  ////////////////////////////
  // Pointer-like interface. This forwards the calls to the relevant Pointer
  // functions (if Pointer object is present). Otherwise operates directly on
  // this location.
  //
  // They are defined later because they need the Pointer class to be defined.
  //
  // TODO: rethink this API so that Pointer-related functions don't pollute the
  // Location interface.
  ////////////////////////////

  Object *Follow();
  void Put(std::unique_ptr<Object> obj);
  std::unique_ptr<Object> Take();

  ////////////////////////////
  // Task queue interface.
  //
  // These functions are defined later because they need Task classes to be
  // defined.
  //
  // TODO: rethink this API so that Task-related functions don't pollute the
  // Location interface.
  ////////////////////////////

  // Schedule this object's Updated function to be executed with the `updated`
  // argument.
  void ScheduleLocalUpdate(Location &updated);

  // Add this object to the task queue. Once it's turn comes, its `Run` method
  // will be executed.
  void ScheduleRun();

  // Execute this object's Errored function using the task queue.
  void ScheduleErrored(Location &errored);

  ////////////////////////////
  // Misc
  ////////////////////////////

  // Iterate over all nearby objects (including this object).
  //
  // Return non-null from the callback to stop the search.
  void *Nearby(std::function<void *(Location &)> callback);

  // This function should register a connection from this location to the
  // `other` so that subsequent calls to `Find` will return `other`.
  //
  // The function tries to be clever and marks the connection as `to_direct` if
  // the current object defines an argument with the same type as the `other`
  // object.
  //
  // This function should also notify the object with the `ConnectionAdded`
  // call.
  Connection *ConnectTo(Location &other, std::string_view label,
                        Connection::PointerBehavior pointer_behavior =
                            Connection::kFollowPointers);

  // Immediately execute this object's Updated function.
  void Updated(Location &updated) { object->Updated(*this, updated); }

  // Call this function when the value of the object has changed.
  //
  // It will notify all observers & call their `Updated` function.
  //
  // The `Updated` function will not be called immediately but will be scheduled
  // using the task queue.
  void ScheduleUpdate() {
    for (auto observer : update_observers) {
      observer->ScheduleLocalUpdate(*this);
    }
  }

  void ObserveUpdates(Location &other) {
    other.update_observers.insert(this);
    observing_updates.insert(&other);
  }

  void StopObservingUpdates(Location &other) {
    other.update_observers.erase(this);
    observing_updates.erase(&other);
  }

  void ObserveErrors(Location &other) {
    other.error_observers.insert(this);
    observing_errors.insert(&other);
  }

  std::string GetText() {
    auto *follow = Follow();
    if (follow == nullptr) {
      return "";
    }
    return follow->GetText();
  }
  double GetNumber() { return std::stod(GetText()); }

  // Immediately execute this object's Run function.
  void Run() { object->Run(*this); }

  // Immediately execute this object's Errored function.
  void Errored(Location &errored) { object->Errored(*this, errored); }

  Location *Rename(std::string_view new_name) {
    name = new_name;
    return this;
  }

  template <typename T> T *ThisAs() { return dynamic_cast<T *>(object.get()); }
  template <typename T> T *As() { return dynamic_cast<T *>(Follow()); }
  template <typename T> T *ParentAs() const {
    return parent ? dynamic_cast<T *>(parent->object.get()) : nullptr;
  }

  void SetText(std::string_view text) {
    std::string current_text = GetText();
    if (current_text == text) {
      return;
    }
    Follow()->SetText(*this, text);
    ScheduleUpdate();
  }
  void SetNumber(double number);

  vec2 AnimatedPosition(animation::State &animation_state) const;
  void Draw(SkCanvas &canvas, animation::State &animation_state) const override;
  std::unique_ptr<Action> ButtonDownAction(gui::Pointer &,
                                           gui::PointerButton) override;
  SkPath Shape() const override;
  gui::VisitResult VisitImmediateChildren(gui::WidgetVisitor &visitor) override;

  // Add ConnectionWidgets for all arguments defined by the objects.
  void UpdateConnectionWidgets();

  ////////////////////////////
  // Error reporting
  ////////////////////////////

  // First error caught by this Location.
  std::unique_ptr<Error> error;

  // These functions are defined later because they use the Machine class which
  // needs to be defined first.
  //
  // TODO: rethink this API so that Machine-related functions don't pollute the
  // Location interface.
  bool HasError();
  Error *GetError();
  void ClearError();

  Error *
  ReportError(std::string_view message,
              std::source_location location = std::source_location::current()) {
    if (error == nullptr) {
      error.reset(new Error(message, location));
      error->source = this;
      for (auto observer : error_observers) {
        observer->ScheduleErrored(*this);
      }
      if (parent) {
        parent->ScheduleErrored(*this);
      }
    }
    return error.get();
  }

  // Shorthand function for reporting that a required property couldn't be
  // found.
  void ReportMissing(std::string_view property);

  std::string LoggableString() const;
};

} // namespace automat