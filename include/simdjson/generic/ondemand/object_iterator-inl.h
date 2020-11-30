namespace simdjson {
namespace SIMDJSON_IMPLEMENTATION {
namespace ondemand {

//
// object_iterator
//

simdjson_really_inline object_iterator::object_iterator(const value_iterator &_iter) noexcept
  : iter{_iter},
    at_start{true}
{}

simdjson_really_inline simdjson_result<field> object_iterator::operator*() noexcept {
  error_code error = iter.error();
  if (error) { iter.abandon(); return error; }
  auto result = field::start(iter);
  // TODO this is a safety rail ... users should exit loops as soon as they receive an error.
  // Nonetheless, let's see if performance is OK with this if statement--the compiler may give it to us for free.
  if (result.error()) { iter.abandon(); }
  return result;
}
simdjson_really_inline bool object_iterator::operator==(const object_iterator &other) const noexcept {
  return !(*this != other);
}
simdjson_really_inline bool object_iterator::operator!=(const object_iterator &) const noexcept {
  return iter.is_open();
}
simdjson_really_inline object_iterator &object_iterator::operator++() noexcept {
  // TODO this is a safety rail ... users should exit loops as soon as they receive an error.
  // Nonetheless, let's see if performance is OK with this if statement--the compiler may give it to us for free.
  if (!iter.is_open()) { return *this; } // Iterator will be released if there is an error

  simdjson_unused error_code error;
  if ((error = iter.finish_child() )) { return *this; }

  simdjson_unused bool has_value;
  if ((error = iter.has_next_field().get(has_value) )) { return *this; };
  return *this;
}

//
// ### Live States
//
// While iterating or looking up values, depth >= iter.depth. at_start may vary. Error is
// always SUCCESS:
//
// - Start: This is the state when the object is first found and the iterator is just past the {.
//   In this state, at_start == true.
// - Next: After we hand a scalar value to the user, or an array/object which they then fully
//   iterate over, the iterator is at the , or } before the next value. In this state,
//   depth == iter.depth, at_start == false, and error == SUCCESS.
// - Unfinished Business: When we hand an array/object to the user which they do not fully
//   iterate over, we need to finish that iteration by skipping child values until we reach the
//   Next state. In this state, depth > iter.depth, at_start == false, and error == SUCCESS.
//
// ## Error States
//
// In error states, we will yield exactly one more value before stopping. iter.depth == depth
// and at_start is always false. We decrement after yielding the error, moving to the Finished
// state.
//
// - Chained Error: When the object iterator is part of an error chain--for example, in
//   `for (auto tweet : doc["tweets"])`, where the tweet field may be missing or not be an
//   object--we yield that error in the loop, exactly once. In this state, error != SUCCESS and
//   iter.depth == depth, and at_start == false. We decrement depth when we yield the error.
// - Missing Comma Error: When the iterator ++ method discovers there is no comma between fields,
//   we flag that as an error and treat it exactly the same as a Chained Error. In this state,
//   error == TAPE_ERROR, iter.depth == depth, and at_start == false.
//
// Errors that occur while reading a field to give to the user (such as when the key is not a
// string or the field is missing a colon) are yielded immediately. Depth is then decremented,
// moving to the Finished state without transitioning through an Error state at all.
//
// ## Terminal State
//
// The terminal state has iter.depth < depth. at_start is always false.
//
// - Finished: When we have reached a }, we are finished. We signal this by decrementing depth.
//   In this state, iter.depth < depth, at_start == false, and error == SUCCESS.
//

simdjson_warn_unused simdjson_really_inline error_code object_iterator::find_field_raw(const std::string_view key) noexcept {
  if (!iter.is_open()) { return NO_SUCH_FIELD; }

  // Unless this is the first field, we need to advance past the , and check for }
  error_code error;
  bool has_value;
  if (at_start) {
    at_start = false;
    has_value = true;
  } else {
    if ((error = iter.finish_child() )) { iter.abandon(); return error; }
    if ((error = iter.has_next_field().get(has_value) )) { iter.abandon(); return error; }
  }
  while (has_value) {
    // Get the key
    raw_json_string actual_key;
    if ((error = iter.field_key().get(actual_key) )) { iter.abandon(); return error; };
    if ((error = iter.field_value() )) { iter.abandon(); return error; }

    // Check if it matches
    if (actual_key == key) {
      logger::log_event(iter, "match", key, -2);
      return SUCCESS;
    }
    logger::log_event(iter, "no match", key, -2);
    SIMDJSON_TRY( iter.skip_child() ); // Skip the value entirely
    if ((error = iter.has_next_field().get(has_value) )) { iter.abandon(); return error; }
  }

  // If the loop ended, we're out of fields to look at.
  return NO_SUCH_FIELD;
}

} // namespace ondemand
} // namespace SIMDJSON_IMPLEMENTATION
} // namespace simdjson

namespace simdjson {

simdjson_really_inline simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>::simdjson_result(
  SIMDJSON_IMPLEMENTATION::ondemand::object_iterator &&value
) noexcept
  : implementation_simdjson_result_base<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>(std::forward<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>(value))
{
}
simdjson_really_inline simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>::simdjson_result(error_code error) noexcept
  : implementation_simdjson_result_base<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>({}, error)
{
}

simdjson_really_inline simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::field> simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>::operator*() noexcept {
  if (error()) { second = SUCCESS; return error(); }
  return *first;
}
// Assumes it's being compared with the end. true if depth < iter->depth.
simdjson_really_inline bool simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>::operator==(const simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator> &other) const noexcept {
  if (error()) { return true; }
  return first == other.first;
}
// Assumes it's being compared with the end. true if depth >= iter->depth.
simdjson_really_inline bool simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>::operator!=(const simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator> &other) const noexcept {
  if (error()) { return false; }
  return first != other.first;
}
// Checks for ']' and ','
simdjson_really_inline simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator> &simdjson_result<SIMDJSON_IMPLEMENTATION::ondemand::object_iterator>::operator++() noexcept {
  if (error()) { return *this; }
  ++first;
  return *this;
}

} // namespace simdjson
