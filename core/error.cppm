module;
#include <system_error>
#include <source_location>

export module herodotus.clio.error;

export namespace herodotus::clio
{
class Exception : public std::system_error
{
public:
    Exception(std::error_code ec, const std::string& what_arg, std::source_location location = std::source_location::current()) : 
        std::system_error(ec, what_arg), 
        location_(location)
    {}

    Exception(std::error_code ec, std::source_location location = std::source_location::current()) : 
        std::system_error(ec),
        location_(location)
    {}

    Exception(const Exception&) = default;
    Exception(Exception&&) = default;

    const std::source_location& location() const noexcept
    {
        return location_;
    }

private:
    std::source_location location_;
};
}