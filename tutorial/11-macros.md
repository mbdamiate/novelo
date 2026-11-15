# 11. Macros

Chapter 2 pointed out that Novelo code is written as lists, the same lists a program can build and take apart. A macro uses that fact. It is a function that runs before evaluation, receives the code of its arguments as data, and returns new code to evaluate in its place. With macros, a program can add forms to the language.

This is the most powerful feature of a Lisp, and the one to use least. Read the last section before writing your first macro.

## Quasiquote: templates of code

A macro returns code, and code is lists, so a macro needs a convenient way to build lists. The backquote `` ` `` quotes a list like `'` does, but a comma `,` inside it evaluates one part, and `,@` splices a list into place:

```novelo
> (define x 1)
> (define xs (list 2 3))
> `(a ,x)
(a 1)
> `(a ,x ,@xs)
(a 1 2 3)
> `(if ,x "yes" otherwise "no")
(if 1 "yes" otherwise "no")
```

The last result is an `if` expression, as data. It has not been evaluated.

## `define-macro`

`define-macro` binds a name to a function that transforms code. When the evaluator meets `(name argument …)`, it calls the function with the arguments as they are written, unevaluated. Then it evaluates whatever code the function returns.

`unless` runs its body only when a test is false:

```novelo
> (define-macro unless
>   (lambda (test &rest body)
>     `(if ,test #void otherwise (sequence ,@body))))
> (unless (> 1 2) "ran")
"ran"
> (unless (< 1 2) "ran")
```

`(unless (> 1 2) "ran")` becomes `(if (> 1 2) #void otherwise (sequence "ran"))`, which is then evaluated as usual. A function could not do this, because a function's arguments are all evaluated before it runs, so the body would run whatever the test said.

A macro can also see the code it was given. `show` returns an expression together with its value:

```novelo
> (define-macro show
>   (lambda (expression)
>     `(list (quote ,expression) ,expression)))
> (show (+ 1 2))
((+ 1 2) 3)
```

The test harness of the exercises does this: `check` is a macro, so a failing test can print the expression it ran. See [`exercises/harness.nvl`](exercises/harness.nvl).

A macro must be defined before its first use in the file, because each form is expanded just before it runs. Its name has no value, so using it as a variable is an error:

```novelo
> (define-macro unless
>   (lambda (test &rest body)
>     `(if ,test #void otherwise (sequence ,@body))))
> unless
error :unbound-symbol: unbound symbol unless unless
```

## Name capture and `generate-symbol`

A macro often needs a local name of its own. Here is an `either` that returns its first argument unless it is `#false`, and evaluates that argument only once:

```novelo
> (define-macro naive-either
>   (lambda (a b)
>     `(let ((value ,a))
>        (if value value otherwise ,b))))
> (define value 5)
> (naive-either #false value)
#false
```

The answer should be 5. Macros in Novelo are not *hygienic*: the `value` the caller passed is pasted inside the macro's `let`, where it means the macro's local, which is `#false`. The fix is a name no one else can write. `generate-symbol` makes a new, unique symbol at each call:

```novelo
> (define-macro either
>   (lambda (a b)
>     (let ((value (generate-symbol)))
>       `(let ((,value ,a))
>          (if ,value ,value otherwise ,b)))))
> (define value 5)
> (either #false value)
5
```

Use `generate-symbol` for every local name a macro introduces.

## Looking at an expansion

A macro's transformer is an ordinary function. Define it with `define` first, and you can call it on quoted code to see what the macro will produce:

```novelo
> (define unless-expansion
>   (lambda (test &rest body)
>     `(if ,test #void otherwise (sequence ,@body))))
> (unless-expansion '(> x 1) '(display! "small"))
(if (> x 1) #void otherwise (sequence (display! "small")))
> (define-macro unless unless-expansion)
```

## When not to write a macro

A macro is the right tool only when a function cannot do the job, which means one of these:
- control over whether, when, or how often arguments are evaluated (`unless`, `either`);
- access to the code as written (`show`, `check`);
- a new binding form, which introduces names.

Everything else is better as a function. Functions are values: they can be passed to `map` and stored in maps, and they appear in traces. Macros are none of those things, and their mistakes appear in code you never wrote.

Novelo's own derived forms, `let`, `and`, `or`, `match`, `try` and `chain`, are rewrites of exactly this kind, built into the expander ([core §8.3](../core-specification.md#83-derived-forms)).

## Exercises

- **`11-macros`.** Write three macros:
  - `when`;
  - `with-default`, which turns any error into a fallback value;
  - `check-that`, which reports a failed check with the expression that failed.

```bash
tutorial/check.sh 11-macros
```

The example [`09-macros`](../examples/09-macros/main.nvl) builds `when`, `unless` and a `cond`, and shows capture again.

Next: [A tour of the standard library](12-standard-library.md).
