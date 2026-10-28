# Widget framework only. Concrete widgets live in widgets/ (target yasb_widgets).
target_sources(yasb_ui PRIVATE
  backdrop.h
  backdrop.cpp
  base_widget.h
  base_widget.cpp
  callbacks.h
  callbacks.cpp
  event_bus.h
  label_format.h
  label_format.cpp
  polling_worker.h
  polling_worker.cpp
  polling_worker_registry.h
  polling_worker_registry.cpp
  popup.h
  popup.cpp
  registry.h
  registry.cpp
  scrolling_label.h
  scrolling_label.cpp
  stat_popup.h
  stat_popup.cpp
  tooltip.h
  tooltip.cpp
  widget_builder.h
  widget_builder.cpp
)
