/* Zhaoxu OS - inline SVG icons for dock entries (rule 22). */
#include "ade.h"
#include "ade_internal.h"

namespace {

int same_text(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a == (unsigned char)*b;
}

const char kWelcome[] =
    "<svg viewBox=\"0 0 48 48\">"
    "<rect x=\"2\" y=\"2\" width=\"44\" height=\"44\" rx=\"12\" fill=\"#1E6FB8\"/>"
    "<circle cx=\"24\" cy=\"23\" r=\"13\" fill=\"#FFFFFF\"/>"
    "<circle cx=\"19\" cy=\"20\" r=\"2\" fill=\"#1E6FB8\"/>"
    "<circle cx=\"29\" cy=\"20\" r=\"2\" fill=\"#1E6FB8\"/>"
    "<path d=\"M17 27 C19 31 29 31 31 27 L29 26 C27 29 21 29 19 26 Z\" fill=\"#1E6FB8\"/>"
    "</svg>";

const char kCalc[] =
    "<svg viewBox=\"0 0 48 48\">"
    "<rect x=\"2\" y=\"2\" width=\"44\" height=\"44\" rx=\"12\" fill=\"#8E7CC3\"/>"
    "<rect x=\"8\" y=\"7\" width=\"32\" height=\"9\" rx=\"3\" fill=\"#F7FAFD\"/>"
    "<rect x=\"8\" y=\"21\" width=\"8\" height=\"6\" rx=\"2\" fill=\"#F7FAFD\"/>"
    "<rect x=\"20\" y=\"21\" width=\"8\" height=\"6\" rx=\"2\" fill=\"#F7FAFD\"/>"
    "<rect x=\"32\" y=\"21\" width=\"8\" height=\"6\" rx=\"2\" fill=\"#F7FAFD\"/>"
    "<rect x=\"8\" y=\"31\" width=\"8\" height=\"6\" rx=\"2\" fill=\"#F7FAFD\"/>"
    "<rect x=\"20\" y=\"31\" width=\"8\" height=\"6\" rx=\"2\" fill=\"#F7FAFD\"/>"
    "<rect x=\"32\" y=\"31\" width=\"8\" height=\"6\" rx=\"2\" fill=\"#1E6FB8\"/>"
    "</svg>";

const char kInstall[] =
    "<svg viewBox=\"0 0 48 48\">"
    "<rect x=\"2\" y=\"2\" width=\"44\" height=\"44\" rx=\"12\" fill=\"#2E7D32\"/>"
    "<rect x=\"21\" y=\"9\" width=\"6\" height=\"18\" fill=\"#FFFFFF\"/>"
    "<rect x=\"13\" y=\"25\" width=\"22\" height=\"6\" rx=\"2\" fill=\"#FFFFFF\"/>"
    "<rect x=\"12\" y=\"35\" width=\"24\" height=\"5\" rx=\"2\" fill=\"#FFFFFF\"/>"
    "</svg>";

const char kAbout[] =
    "<svg viewBox=\"0 0 48 48\">"
    "<circle cx=\"24\" cy=\"24\" r=\"22\" fill=\"#1E6FB8\"/>"
    "<rect x=\"21\" y=\"12\" width=\"6\" height=\"6\" rx=\"3\" fill=\"#FFFFFF\"/>"
    "<rect x=\"21\" y=\"22\" width=\"6\" height=\"16\" rx=\"3\" fill=\"#FFFFFF\"/>"
    "</svg>";

} // namespace

const char *ade_icon_for_title(const char *title)
{
    if (!title)
        return nullptr;
    if (same_text(title, "欢迎"))
        return kWelcome;
    if (same_text(title, "计算器"))
        return kCalc;
    if (same_text(title, "安装"))
        return kInstall;
    return nullptr;
}

const char *ade_icon_about(void)
{
    return kAbout;
}
