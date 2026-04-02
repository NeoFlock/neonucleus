# Coding style

NN and NCL have a very specific API and code style.
This is meant to be consistent everywhere to keep the API and codebase easy to learn, navigate, use and maintain.

## Separation of files

- `src/neonucleus.h` for the header to NeoNucleus, the engine. This provides the systems everything interfaces with.
- `src/neonucleus.c`, the implementation of stuff defined in its header file.
- `src/ncomplib.h` for the header to the NeoNucleus Component Library, providing complete implementations for various components.
- `src/ncomplib.c` for the implementation of stuff defined in its header file.
- `src/main.c` for the test emulator used purely to try out the engine and see if it works.

The rationale for single C-file libraries is that they are easy to download and compile. This can simplify the process of updating and compiling into projects.
The reason for using this over shared libraries is that NN makes no ABI stability guarantees. It also makes it super easy to vendor the library,
meaning you can trivially pin to a specific version, and even a trivial build system can work. The reason for not going full STB single-file is that
LSPs can struggle to give diagnostics for the implementation side.

## General coding rules

- For actual source code, aim for 80 cols max on 4-space indentation. This isn't a hard rule, but breaking it should be avoided.
- `goto` should be used for trivial cases, such as the classic `goto fail;` pattern, but also to loop (`goto retry;`) or to skip (`goto found;`)
- While commenting is fine, comments should not be needed most of the time. Avoid writing code which needs to be explained through
comments, aim to ensure the code is readable.
- Prefer fixed capacities. This is useful to reduce the effect of memory hogging. If the fixed capacity is small enough to not use much memory,
prefer pre-allocating the maximum capacity to simplify the code and reduce places that can OOM.

## Component classes/implementations

- NN should provide component classes. These are component instances with a `void *` state specified to them, which stores its data in `classState`.
Their job is to define the methods and its docs and validate the methods to convert them into requests specific to the component type, and
send those requests to a handler (a simple function pointer). They should not have their own locks, they should not store their own state.
- NCL should provide component implementations. These do not have custom state or handlers, as they are *complete*. Prefer functions which set
internal state over many parameters in the constructors. NCL should handle the locking, as all components must be thread-safe, in which case
you should avoid wrapping entire handlers in the locks, and instead prefer locking as late as possible and unlocking as early as possible.
This is to give threads exclusive ownership over the potentially shared component for as little time as possible.
- Assume the component user is experimenting, a hacker, or has an RCE in their code. Make sure everything is validated, from internal state to
arguments. Do not trust the user, we do not have the JVM to save us here.
- Assume anything that can error will error at least once and ensure recovery with an exit code and error message, never panic or forcefully crash.
Assume memory allocation will eventually fail. Assume the filesystem will eventually break. Do not abort/exit/panic.
- Use an enum to keep track of method indexes and method count.
- Avoid separate functions for each method handler. Inline them into the single handler that handles dispatch. This not only net-shrinks the codebase,
but also means that navigating by searching for `== NN_<COMPONENT>_<REQUEST>)` (in NCL) or `== NN_<COMPONENT>NUM_<METHOD>)` (in NN) will show the
dispatch logic and implementation.
