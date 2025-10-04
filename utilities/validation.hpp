#ifndef VALIDATION_HPP
#define VALIDATION_HPP

#include <regex>
#include <string>

namespace utilities {

/**
 * TODO: Test invalid email format
 * To validate email addresses, we would need to conform to RFC 5322 format.
 * Some alternatives include, relying on frontend that uses a fully defined
 * parser e.g. https://github.com/jackbearheart/email-addresses
 * or creating an RFC 5322 compliant Boost.parser/Lexy email address parser
 * and using it. As we rely on verifying email with tokens, this may not be
 * bad for normal application functionality. But a good validator prevent
 * DDos with invalid emails and other issues.
 * And no don't rely on Regex:
 * https://stackoverflow.com/questions/201323/how-can-i-validate-an-email-address-using-a-regular-expression
 *
 **/
// Quick and dirty RFC 5322 compliant email validator (basic)
inline bool is_email_valid(const std::string& email) {
  // Very basic regex for demonstration, not fully RFC compliant but covers most
  // cases
  // see https://stackoverflow.com/a/14075810 for a more detailed one
  // but it is nots yet implementable in C++ ?
  // Basic email regex: local@domain.tld (not fully RFC 5322 compliant)
  const std::regex pattern(
      R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");

  return std::regex_match(email, pattern);
}

}  // namespace utilities

#endif  // VALIDATION_HPP
