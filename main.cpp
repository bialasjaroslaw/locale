#include <iomanip>
#include <iostream>

#include <boost/locale.hpp>
#include <boost/locale/format.hpp>
#include <boost/locale/localization_backend.hpp>
#include <boost/locale/util.hpp>
#include <unicode/dcfmtsym.h>
#include <unicode/decimfmt.h>
#include <unicode/locid.h>
#include <unicode/numberformatter.h>
#include <unicode/ucnv.h>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <magic_enum.hpp>

#if defined(QT_BACKEND)
#include <QLocale>
#include <QString>
#endif

template <typename T>
T convertUnicodeString(const icu::UnicodeString& ustring)
{
    std::string u8str;
    ustring.toUTF8String(u8str);
    return u8str;
};

template <typename CharType>
struct icu_numpunct : public std::numpunct<CharType>
{
    typedef std::basic_string<CharType> string_type;

public:
    icu_numpunct(icu::Locale const& loc)
    {
        UErrorCode err = U_ZERO_ERROR;
        icu::DecimalFormatSymbols syms(loc, err);
        auto decimal_symbol = syms.getSymbol(icu::DecimalFormatSymbols::kDecimalSeparatorSymbol);
        auto separator_symbol = syms.getSymbol(icu::DecimalFormatSymbols::kGroupingSeparatorSymbol);

        UNumberFormat* numformat = unum_open(UNUM_DECIMAL, nullptr, 0, loc.getBaseName(), nullptr, &err);
        char size = unum_getAttribute(numformat, UNUM_GROUPING_SIZE);
        unum_close(numformat);

        decimal_point_ = convertUnicodeString<string_type>(decimal_symbol);
        thousands_sep_ = convertUnicodeString<string_type>(separator_symbol);
        grouping_ = std::string(&size, 1);
        if (decimal_point_.size() > 1)
        {
            std::string decimal_point;
            for (uint8_t c : decimal_point_)
                decimal_point += fmt::format("{:#x} ", c);
            fmt::print("Truncating decimal separator '{}' - {}\n", decimal_point_, decimal_point);
            decimal_point_ = CharType('.');
        }
        if (thousands_sep_.size() > 1)
        {
            std::string thousands_sep;
            for (uint8_t c : thousands_sep_)
                thousands_sep += fmt::format("{:#x} ", c);
            fmt::print("Truncating thousands separator '{}' - {}\n", thousands_sep_, thousands_sep);
            thousands_sep_ = CharType(' ');
        }
    }

protected:
    virtual CharType do_decimal_point() const
    {
        return decimal_point_.empty() ? '\0' : *decimal_point_.c_str();
    }
    virtual CharType do_thousands_sep() const
    {
        return thousands_sep_.empty() ? '\0' : *thousands_sep_.c_str();
    }
    virtual string_type do_grouping() const
    {
        return grouping_;
    }

private:
    string_type decimal_point_;
    string_type thousands_sep_;
    string_type grouping_;
};

auto inspect_locale(const std::locale& loc)
{
    auto& facet = std::use_facet<std::numpunct<char>>(loc);
    auto grouping = facet.grouping();
    auto thousands_sep = grouping.empty() ? char() : facet.thousands_sep();
    auto decim_sep = facet.decimal_point();

    auto grouping_string = std::string("");
    for (uint8_t c : grouping)
        grouping_string += fmt::format("{:#x}", c);

    fmt::print("G: '{}', T: '{}' - {:#x}, D: '{}'\n", grouping_string, thousands_sep,
               static_cast<uint8_t>(thousands_sep), decim_sep);
}

std::string get_sep(const std::string& str, char start, char end)
{
    auto it = std::find(str.cbegin(), str.cend(), start);
    if (it == str.cend())
        return {};
    ++it;
    auto end_it = std::find(it, str.cend(), end);
    auto len = std::distance(it, end_it);
    return len == 0 ? std::string() : std::string(str.data() + std::distance(str.cbegin(), it), len);
}

std::string get_thousand_sep(const std::string& str, char start, char end)
{
    return get_sep(str, start, end);
}

std::string get_decim_sep(const std::string& str, char start, char end)
{
    return get_sep(str, start, end);
}

enum class LocaleBackend
{
    FMTBoostICU,
    FMTBoostICUMod,
    FMTBoostPosix,
    FMTBoostStd,
#if defined(QT_BACKEND)
    Qt,
#endif
    StreamBoostICU,
    StreamBoostPosix,
    StreamBoostStd,
};

std::string format_int(int val, const std::string& locale_str, LocaleBackend backend_type)
{
#if defined(QT_BACKEND)
    if (backend_type == LocaleBackend::Qt)
    {
        auto qtLoc = QLocale(QString::fromStdString(locale_str));
        return qtLoc.toString(val).toStdString();
    }
#endif

    auto backend_manager = boost::locale::localization_backend_manager::global();
    if (backend_type == LocaleBackend::FMTBoostPosix || backend_type == LocaleBackend::StreamBoostPosix)
        backend_manager.select("posix");
    else if (backend_type == LocaleBackend::FMTBoostStd || backend_type == LocaleBackend::StreamBoostStd)
        backend_manager.select("std");
    else
        backend_manager.select("icu");

    boost::locale::generator gen(backend_manager);
    auto locale = gen(locale_str);
    if (backend_type == LocaleBackend::FMTBoostICUMod)
    {
        auto country = std::use_facet<boost::locale::info>(locale).country();
        auto language = std::use_facet<boost::locale::info>(locale).language();
        auto encoding = std::use_facet<boost::locale::info>(locale).encoding();
        auto icuLocale = icu::Locale(language.c_str(), country.c_str(), encoding.c_str());
        locale = std::locale(locale, new icu_numpunct<char>(icuLocale));
    }

    if (backend_type == LocaleBackend::StreamBoostICU || backend_type == LocaleBackend::StreamBoostPosix ||
        backend_type == LocaleBackend::StreamBoostStd)
    {
        std::stringstream ss;
        ss.imbue(locale);
        ss << boost::locale::as::number << val;
        return ss.str();
    }

    return fmt::format(locale, "{:L}", val);
}

std::string format_double(double val, int prec, const std::string& locale_str, LocaleBackend backend_type)
{
#if defined(QT_BACKEND)
    if (backend_type == LocaleBackend::Qt)
    {
        auto qtLoc = QLocale(QString::fromStdString(locale_str));
        return qtLoc.toString(val, 'f', prec).toStdString();
    }
#endif

    auto backend = boost::locale::localization_backend_manager::global();
    if (backend_type == LocaleBackend::FMTBoostPosix || backend_type == LocaleBackend::StreamBoostPosix)
        backend.select("posix");
    else if (backend_type == LocaleBackend::FMTBoostStd || backend_type == LocaleBackend::StreamBoostStd)
        backend.select("std");
    else
        backend.select("icu");

    boost::locale::generator gen(backend);
    auto locale = gen(locale_str);
    if (backend_type == LocaleBackend::FMTBoostICUMod)
    {
        auto country = std::use_facet<boost::locale::info>(locale).country();
        auto language = std::use_facet<boost::locale::info>(locale).language();
        auto encoding = std::use_facet<boost::locale::info>(locale).encoding();
        auto icuLocale = icu::Locale(language.c_str(), country.c_str(), encoding.c_str());
        locale = std::locale(locale, new icu_numpunct<char>(icuLocale));
    }

    if (backend_type == LocaleBackend::StreamBoostICU || backend_type == LocaleBackend::StreamBoostPosix ||
        backend_type == LocaleBackend::StreamBoostStd)
    {
        std::stringstream ss;
        ss.imbue(locale);
        ss << std::setprecision(prec) << std::fixed << boost::locale::as::number << val;
        return ss.str();
    }

    return fmt::format(locale, "{:.{}Lf}", val, prec);
}

int main(int argc, char const* argv[])
{
    auto double_val = 1234567.891144;
    auto int_val = 1234567890;
    auto prec = 2;

    for (const auto& locale_str : {"en_US.UTF-8", "pl_PL.UTF-8", "ru_RU.UTF-8"})
    {
        fmt::print("====\nLocale: {}\n====\n", locale_str);
        for (const auto& locale_backend : magic_enum::enum_values<LocaleBackend>())
        {
            fmt::print("==\nBackend: {}\n==\n", magic_enum::enum_name(locale_backend));
            auto dbl_format = format_double(double_val, 2, locale_str, locale_backend);
            auto int_format = format_int(int_val, locale_str, locale_backend);
            fmt::print("DBL {}\n", dbl_format);
            fmt::print("INT {}\n", int_format);
            auto decim_sep = get_decim_sep(dbl_format, '7', '8');
            auto thousands_sep = get_thousand_sep(int_format, '7', '8');
            fmt::print("T: '{}', D: '{}'\n", thousands_sep, decim_sep);
        }

        for (const auto& locale_backend : {"icu", "std", "posix"})
        {
            auto backend = boost::locale::localization_backend_manager::global();
            backend.select(locale_backend);
            boost::locale::generator gen(backend);
            auto locale = gen(locale_str);
            fmt::print("==\nLocale details generated for {} backend:\n==\n", locale_backend);
            inspect_locale(locale);
        }
    }

    return 0;
}