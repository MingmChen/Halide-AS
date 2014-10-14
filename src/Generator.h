#ifndef HALIDE_GENERATOR_H_
#define HALIDE_GENERATOR_H_

/** \file
 *
 * Generator is a class used to encapsulate the building of Funcs in user pipelines.
 * A Generator is agnostic to JIT vs AOT compilation; it can be used for either
 * purpose, but is especially convenient to use for AOT compilation.
 *
 * A Generator automatically detects the run-time parameters (Param/ImageParams)
 * associated with the Func and (for AOT code) produces a function signature
 * with the correct params in the correct order.
 *
 * A Generator can also be customized via compile-time parameters (GeneratorParams),
 * which affect code generation.
 *
 * GeneratorParams, ImageParams, and Params are (by convention)
 * always public and always declared at the top of the Generator class,
 * in the order
 *
 *    GeneratorParam(s)
 *    ImageParam(s)
 *    Param(s)
 *
 * Preferred style is to use C++11 in-class initialization style, e.g.
 * \code
 *    GeneratorParam<int> magic{"magic", 42};
 * \endcode
 *
 * Note that the ImageParams/Params will appear in the C function
 * call in the order they are declared. (GeneratorParams are always
 * referenced by name, not position, so their order is irrelevant.)
 *
 * All Param variants declared as Generator members must have explicit
 * names, and all such names must match the regex [A-Za-z_][A-Za-z_0-9]*
 * (i.e., essentially a C/C++ variable name). (Note that autogenerated
 * Param names do not match this pattern, and thus will be rejected.)
 * By convention, the name should match the member-variable name.
 *
 * Generators are usually added to a global registry to simplify AOT build mechanics;
 * this is done by simply defining an instance of RegisterGenerator at static
 * scope:
 * \code
 *    RegisterGenerator<ExampleGen> register_jit_example("jit_example");
 * \endcode
 *
 * "jit_example" is the registered name of the Generator (which must match the
 * same rules as Param names, above).
 *
 * (If you are jitting, you may not need to bother registering your Generator,
 * but it's considered best practice to always do so anyway.)
 *
 * Most Generator classes will only need to override the build() method, and perhaps
 * declare a Param and/or GeneratorParam:
 *
 * \code
 *  class XorImage : public Generator<XorImage> {
 *  public:
 *      GeneratorParam<int> channels{"channels", 3};
 *      ImageParam input{UInt(8), 3, "input"};
 *      Param<uint8_t> mask{"mask"};
 *
 *      Func build() override {
 *          Var x, y, c;
 *          Func f;
 *          f(x, y, c) = input(x, y, c) ^ mask;
 *          f.bound(c, 0, bound).reorder(c, x, y).unroll(c);
 *          return f;
 *      }
 *  };
 *  RegisterGenerator<XorImage> reg_xor("xor_image");
 * \endcode
 *
 * By default, this code schedules itself for 3-channel (RGB) images;
 * by changing the value of the "channels" GeneratorParam before calling
 * build() we can produce code suited for different channel counts.
 *
 * Note that a Generator is always executed with a specific Target
 * assigned to it, that you can access via the get_target() method.
 * (You should *not* use the global get_target_from_environment(), etc.
 * methods provided in Target.h)
 */

// Generator requires C++11
#if __cplusplus > 199711L

#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "Func.h"
#include "ObjectInstanceRegistry.h"
#include "Target.h"

namespace Halide {

namespace Internal {

/** generate_filter_main() is a convenient wrapper for GeneratorRegistry::create() +
 * compile_to_files();
 * it can be trivially wrapped by a "real" main() to produce a command-line utility
 * for ahead-of-time filter compilation. */
EXPORT int generate_filter_main(int argc, char **argv, std::ostream &cerr);

class GeneratorParamBase {
public:
    explicit GeneratorParamBase(const std::string &name);
    virtual ~GeneratorParamBase();
    virtual void set_from_string(const std::string &value_string) = 0;

    const std::string name;
};

}  // namespace Internal

/** GeneratorParam is a templated class that can be used to modify the behavior
 * of the Generator at code-generation time. GeneratorParams are commonly
 * specified in build files (e.g. Makefile) to customize the behavior of
 * a given Generator, thus they have a very constrained set of types to allow
 * for efficient specification via command-line flags. A GeneratorParm can be:
 *   - any float or int type.
 *   - bool
 *   - enum
 *   - Halide::Target
 * All GeneratorParams have a default value. Arithmetic types can also
 * optionally specify min and max. Enum types must specify a string-to-value
 * map.
 */
template <typename T> class GeneratorParam : public Internal::GeneratorParamBase {
public:
    template <typename T2 = T,
              typename std::enable_if<std::is_same<T2, Target>::value>::type * = nullptr>
    GeneratorParam(const std::string &name, const T &value)
        : GeneratorParamBase(name), value(value), min(value), max(value) {}

    // Note that "is_arithmetic" includes the bool type.
    template <typename T2 = T,
              typename std::enable_if<std::is_arithmetic<T2>::value>::type * = nullptr>
    GeneratorParam(const std::string &name, const T &value)
        : GeneratorParamBase(name), value(value), min(std::numeric_limits<T>::lowest()),
          max(std::numeric_limits<T>::max()) {}

    template <typename T2 = T,
              typename std::enable_if<std::is_arithmetic<T2>::value &&
                                      !std::is_same<T2, bool>::value>::type * = nullptr>
    GeneratorParam(const std::string &name, const T &value, const T &min, const T &max)
        : GeneratorParamBase(name),
          // Use the set() method so that out-of-range values are checked.
          // value(std::min(std::max(value, min), max)),
          min(min), max(max) {
        static_assert(std::is_arithmetic<T>::value && !std::is_same<T, bool>::value,
                      "Only arithmetic types may specify min and max");
        set(value);
    }

    template <typename T2 = T, typename std::enable_if<std::is_enum<T2>::value>::type * = nullptr>
    GeneratorParam(const std::string &name, const T &value,
                   const std::map<std::string, T> &enum_map)
        : GeneratorParamBase(name), value(value), min(std::numeric_limits<T>::lowest()),
          max(std::numeric_limits<T>::max()), enum_map(enum_map) {
        static_assert(std::is_enum<T>::value, "Only enum types may specify value maps");
    }

    // Arithmetic values must fall within the range -- we don't silently clamp.
    template <typename T2 = T,
              typename std::enable_if<std::is_arithmetic<T2>::value>::type * = nullptr>
    void set(const T &new_value) {
        user_assert(new_value >= min && new_value <= max) << "Value out of range: " << new_value;
        value = new_value;
    }

    template <typename T2 = T,
              typename std::enable_if<!std::is_arithmetic<T2>::value>::type * = nullptr>
    void set(const T &new_value) {
        value = new_value;
    }
    void set_from_string(const std::string &new_value_string) override {
        // delegate to a function that we can specialize based on the template argument
        set(parse(new_value_string));
    }
    operator const T &() const { return value; }
    operator Expr() const { return value; }

private:
    T value;
    const T min, max;  // only for arithmetic types
    const std::map<std::string, T> enum_map;  // only for enums

    static T lookup(const std::string &key, const std::map<std::string, T> &value_map) {
        auto it = value_map.find(key);
        user_assert(it != value_map.end()) << "Enumeration value not found: " << key;
        return it->second;
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_same<T2, Target>::value>::type * = nullptr>
    T parse(const std::string &s) {
        return parse_target_string(s);
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_same<T2, bool>::value>::type * = nullptr>
    T parse(const std::string &s) {
        if (s == "true") return true;
        if (s == "false") return false;
        user_assert(false) << "Unable to parse bool: " << s;
        return false;
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_integral<T2>::value &&
                                      !std::is_same<T2, bool>::value>::type * = nullptr>
    T parse(const std::string &s) {
        std::istringstream iss(s);
        T t;
        iss >> t;
        user_assert(!iss.fail()) << "Unable to parse integer: " << s;
        return t;
    }

    template <typename T2 = T,
              typename std::enable_if<std::is_floating_point<T2>::value>::type * = nullptr>
    T parse(const std::string &s) {
        std::istringstream iss(s);
        T t;
        iss >> t;
        user_assert(!iss.fail()) << "Unable to parse float: " << s;
        return t;
    }

    template <typename T2 = T, typename std::enable_if<std::is_enum<T2>::value>::type * = nullptr>
    T parse(const std::string &s) {
        return lookup(s, enum_map);
    }
};

namespace Internal {

using GeneratorParamValues = std::map<std::string, std::string>;

class NamesInterface {
    // Names in this class are only intended for use in derived classes.
protected:
    // Import a consistent list of Halide names that can be used in
    // Halide generators without qualification.
    using Var = Halide::Var;
    using Expr = Halide::Expr;
    using Func = Halide::Func;
    using RDom = Halide::RDom;
    using ImageParam = Halide::ImageParam;
    template <typename T> using GeneratorParam = Halide::GeneratorParam<T>;
    template <typename T> using Param = Halide::Param<T>;
    template <typename T> Expr cast(Expr e) const { return Halide::cast<T>(e); }
};

class GeneratorBase : public NamesInterface {
public:
    GeneratorParam<Target> target{ "target", Halide::get_jit_target_from_environment() };

    struct EmitOptions {
        bool emit_o, emit_h, emit_cpp, emit_assembly, emit_bitcode, emit_stmt, emit_stmt_html;
        EmitOptions()
            : emit_o(true), emit_h(true), emit_cpp(false), emit_assembly(false),
              emit_bitcode(false), emit_stmt(false), emit_stmt_html(false) {}
    };

    virtual ~GeneratorBase();

    virtual Func build() = 0;

    const Target &get_target() const { return target; }

    void set_generator_param_values(const GeneratorParamValues &params);

    std::vector<Argument> get_filter_arguments() const;

    // Call build() and produce compiled output of the given func.
    // All files will be in the given directory, with the given file_base_name
    // plus an appropriate extension. If file_base_name is empty, function_name
    // will be used as file_base_name.
    void emit_filter(const std::string &output_dir, const std::string &function_name,
                     const std::string &file_base_name = "", const EmitOptions &options = EmitOptions());

protected:
    GeneratorBase(size_t size);

private:
    const size_t size;

    std::vector<Argument> filter_arguments;
    std::map<std::string, Internal::Parameter *> filter_params;
    std::map<std::string, Internal::GeneratorParamBase *> generator_params;
    bool params_built;

    void build_params();

    // Provide private, unimplemented, wrong-result-type methods here
    // so that Generators don't attempt to call the global methods
    // of the same name by accident: use the get_target() method instead.
    void get_host_target();
    void get_jit_target_from_environment();
    void get_target_from_environment();

    GeneratorBase(const GeneratorBase &) = delete;
    void operator=(const GeneratorBase &) = delete;
};

class GeneratorFactory {
public:
    virtual ~GeneratorFactory() {}
    virtual std::unique_ptr<GeneratorBase> create(const GeneratorParamValues &params) const = 0;
};

class GeneratorRegistry {
public:
    static void register_factory(const std::string &name,
                                 std::unique_ptr<GeneratorFactory> factory);
    static void unregister_factory(const std::string &name);
    static std::vector<std::string> enumerate();
    static std::unique_ptr<GeneratorBase> create(const std::string &name,
                                                 const GeneratorParamValues &params);

private:
    using GeneratorFactoryMap = std::map<const std::string, std::unique_ptr<GeneratorFactory> >;

    GeneratorFactoryMap factories;
    std::mutex mutex;

    static GeneratorRegistry &get_registry();

    GeneratorRegistry() {}
    GeneratorRegistry(const GeneratorRegistry &) = delete;
    void operator=(const GeneratorRegistry &) = delete;
};

}  // namespace Internal

template <class T> class Generator : public Internal::GeneratorBase {
public:
    Generator() : Internal::GeneratorBase(sizeof(T)) {}
};

template <class T> class RegisterGenerator {
private:
    class TFactory : public Internal::GeneratorFactory {
    public:
        virtual std::unique_ptr<Internal::GeneratorBase>
        create(const Internal::GeneratorParamValues &params) const {
            std::unique_ptr<Internal::GeneratorBase> g(new T());
            g->set_generator_param_values(params);
            return g;
        }
    };

public:
    explicit RegisterGenerator(const std::string &name) {
        std::unique_ptr<Internal::GeneratorFactory> f(new TFactory());
        Internal::GeneratorRegistry::register_factory(name, std::move(f));
    }
};

}  // namespace Halide

#endif  // __cplusplus > 199711L

#endif  // HALIDE_GENERATOR_H_
