add_executable(event_queue_smoke_test event_queue_smoke_test.cc)
target_link_libraries(
  event_queue_smoke_test
  PRIVATE
    nicloadoff_sim)

add_test(NAME event_queue_smoke_test COMMAND event_queue_smoke_test)
