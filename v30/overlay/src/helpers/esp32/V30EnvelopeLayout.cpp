#include "V30EnvelopeLayout.h"
namespace v30 { namespace envelope {
ParseResult parse(const uint8_t*,size_t,View& out){out=View{};return ParseResult::Invalid;}
}}
