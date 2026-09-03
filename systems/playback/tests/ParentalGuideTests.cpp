// ParentalGuide contract: wire parse, dominant-severity rule, sort, cap.
#include <nuvio/playback/ParentalGuide.h>

#include <cstdio>

using nuvio::playback::ParentalWarning;

static int failures = 0;
#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            ++failures;                             \
            std::fprintf(stderr, "FAIL %s\n", msg); \
        }                                           \
    } while (0)

int main()
{
    using namespace nuvio::playback;

    { // dominant-non-none wins; none-majority drops the category
        const QByteArray body = R"({"parentsGuide":[
            {"category":"VIOLENCE",
             "severityBreakdowns":[
               {"severityLevel":"none","voteCount":50},
               {"severityLevel":"moderate","voteCount":120},
               {"severityLevel":"severe","voteCount":30}]},
            {"category":"SEXUAL_CONTENT",
             "severityBreakdowns":[
               {"severityLevel":"none","voteCount":200},
               {"severityLevel":"mild","voteCount":10}]},
            {"category":"PROFANITY",
             "severityBreakdowns":[
               {"severityLevel":"severe","voteCount":300}]},
            {"category":"UNKNOWN_FUTURE",
             "severityBreakdowns":[
               {"severityLevel":"severe","voteCount":999}]}
        ]})";
        const QList<ParentalWarning> w = parseParentalGuide(body);
        CHECK(w.size() == 2, "dropped none-majority + unknown category");
        CHECK(w[0].label == "Profanity" && w[0].severity == "Severe",
              "severe sorts first with English labels");
        CHECK(w[1].label == "Violence" && w[1].severity == "Moderate",
              "moderate second");
    }

    { // tolerance: garbage and empty shapes yield no warnings, never crash
        CHECK(parseParentalGuide("{}").isEmpty(), "missing list -> empty");
        CHECK(parseParentalGuide("not json").isEmpty(), "garbage -> empty");
        CHECK(parseParentalGuide(R"({"parentsGuide":[]})").isEmpty(),
              "empty list -> empty");
        CHECK(parseParentalGuide(R"({"parentsGuide":[{"category":"VIOLENCE"}]})")
                  .isEmpty(),
              "missing breakdowns -> dropped");
    }

    std::printf(failures ? "PARENTAL SUITE FAILURES=%d\n"
                         : "PARENTAL SUITE OK (%d failures)\n",
                 failures);
    return failures ? 1 : 0;
}
