/* core §8–9 — expansion, evaluation, primitives and import. */
#include "test.h"

#include "error.h"
#include "printer.h"
#include "program.h"

#include <string.h>

/* ---- helpers ---------------------------------------------------------------------- */

typedef struct Run {
    LispState *state;
    LispBuffer output;
    LispRunResult result;
    Value *value;
} Run;

static void run_start(Run *run, const char *source, size_t stack_limit) {
    lisp_buffer_init(&run->output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&run->output), .stack_limit = stack_limit};
    run->state = lisp_state_new(&options);
    run->value = nullptr;
    LispReadError read_error;
    run->result = lisp_run_source(run->state, source, strlen(source), &run->value, &read_error);
}

static void run_finish(Run *run) {
    lisp_release(run->value);
    lisp_state_free(run->state);
    lisp_buffer_free(&run->output);
}

static void describe_failure(const char *file, int line, const char *source, Run *run, const char *expected) {
    LispBuffer description;
    lisp_buffer_init(&description);
    if (run->result == LISP_RUN_ERROR) {
        lisp_describe_error(&description, run->state->error);
    } else if (run->result == LISP_RUN_READ_ERROR) {
        lisp_buffer_append_string(&description, "read error");
    } else {
        lisp_print(&description, run->value, LISP_PRINT_WRITE);
    }
    test_fail(file, line, "%s gave %s, expected %s", source, description.data, expected);
    lisp_buffer_free(&description);
}

/* The program ends normally with a value equal? to expected. */
static void check_run(const char *file, int line, const char *source, const char *expected) {
    Run run;
    run_start(&run, source, 0);
    if (run.result != LISP_RUN_OK) {
        describe_failure(file, line, source, &run, expected);
    } else {
        test_check_equal(file, line, run.value, expected);
    }
    run_finish(&run);
}

/* The program ends with an uncaught error of this :kind (e.g. ":wrong-type"). */
static void check_run_error(const char *file, int line, const char *source, const char *kind) {
    Run run;
    run_start(&run, source, 0);
    LispBuffer actual;
    lisp_buffer_init(&actual);
    Value *actual_kind = run.result == LISP_RUN_ERROR ? lisp_error_kind(run.state) : nullptr;
    if (actual_kind) lisp_print(&actual, actual_kind, LISP_PRINT_WRITE);
    if (!actual_kind || strcmp(actual.data, kind) != 0) describe_failure(file, line, source, &run, kind);
    lisp_buffer_free(&actual);
    run_finish(&run);
}

/* The program prints exactly this text. */
static void check_output(const char *file, int line, const char *source, const char *expected) {
    Run run;
    run_start(&run, source, 0);
    const char *actual = run.output.data ? run.output.data : "";
    if (run.result != LISP_RUN_OK) {
        describe_failure(file, line, source, &run, expected);
    } else if (strcmp(actual, expected) != 0) {
        test_fail(file, line, "%s printed \"%s\", expected \"%s\"", source, actual, expected);
    }
    run_finish(&run);
}

#define CHECK_RUN(source, expected) check_run(__FILE__, __LINE__, (source), (expected))
#define CHECK_RUN_ERROR(source, kind) check_run_error(__FILE__, __LINE__, (source), (kind))
#define CHECK_OUTPUT(source, expected) check_output(__FILE__, __LINE__, (source), (expected))

/* ---- expansion (§8.2–8.3) -------------------------------------------------------------- */

static void t_special_form_shapes(void) {
    CHECK_RUN_ERROR("(if 1)", ":expansion-error");
    CHECK_RUN_ERROR("(if 1 2 else 3)", ":expansion-error");
    CHECK_RUN_ERROR("(quote)", ":expansion-error");
    CHECK_RUN_ERROR("(quote a b)", ":expansion-error");
    CHECK_RUN_ERROR("(lambda (x))", ":expansion-error");
    CHECK_RUN_ERROR("(lambda (x) (declare (x Integer)))", ":expansion-error");
    CHECK_RUN_ERROR("(lambda (&rest) 1)", ":expansion-error");
    CHECK_RUN_ERROR("(lambda (a &rest b c) 1)", ":expansion-error");
    CHECK_RUN_ERROR("(lambda (:k) 1)", ":expansion-error");
    CHECK_RUN_ERROR("(define 1 2)", ":expansion-error");
    CHECK_RUN_ERROR("(define x)", ":expansion-error");
    CHECK_RUN_ERROR("(let ((x)) x)", ":expansion-error");
    CHECK_RUN_ERROR("(let (x 1) x)", ":expansion-error");
    CHECK_RUN_ERROR("(sequence)", ":expansion-error");
    CHECK_RUN_ERROR("(list (sequence))", ":expansion-error");
    CHECK_RUN_ERROR("(let ((x 1)) (define y x))", ":expansion-error");
    CHECK_RUN_ERROR("(list (define-macro m (lambda () 1)))", ":expansion-error");
    CHECK_RUN_ERROR("(list (declare-type f Integer))", ":expansion-error");
    CHECK_RUN_ERROR("`(1 (unquote-splicing))", ":expansion-error");
    CHECK_RUN_ERROR("(unquote-splicing x)", ":expansion-error");
    CHECK_RUN_ERROR("`,@(list 1)", ":expansion-error");
}

static void t_derived_forms(void) {
    CHECK_RUN("(let ((x 1) (y (+ x 1)) (x (* y 10))) (list x y))", "(20 2)");
    CHECK_RUN("(let ((x 1)) (declare (x Integer)) x)", "1");
    CHECK_RUN("(list (and 1) (or 2) (and #false (undefined)) (or 1 (undefined)))", "(1 2 #false 1)");
    CHECK_RUN("(let ((t 1)) (or #false t))", "1"); /* or's temporary does not capture t */
    CHECK_RUN("(sequence (define a 1) (define b (+ a 1)) b)", "2");
}

static void t_quasiquote(void) {
    CHECK_RUN("(let ((xs (list 1 2))) `(,@xs))", "(1 2)");
    CHECK_RUN("(let ((xs ())) `(a ,@xs b))", "(a b)");
    CHECK_RUN("(let ((x 1)) `(,x ,@(list 2 3) ,x))", "(1 2 3 1)");
    CHECK_RUN("`(1 (2 ,(+ 1 2)))", "(1 (2 3))");
    CHECK_RUN("`x", "x");
    CHECK_RUN("`{:a ,(+ 1 1) :b (,(+ 1 2))}", "{:a 2 :b (3)}");
    CHECK_RUN("`(a `(b ,(c ,(+ 1 2))))", "(a (quasiquote (b (unquote (c 3)))))");
    CHECK_RUN("`(a `(b ,@(c ,@(list 1 2))))", "(a (quasiquote (b (unquote-splicing (c 1 2)))))");
    CHECK_RUN_ERROR("(let ((a :x) (b :x)) `{,a 1 ,b 2})", ":duplicate-key");
}

static void t_macros(void) {
    CHECK_RUN("(define-macro unless (lambda (c body) `(if ,c #void otherwise ,body))) (unless #false 5)", "5");
    /* a local name shadows a macro */
    CHECK_RUN("(define-macro m (lambda () 1)) ((lambda (m) (m)) (lambda () 2))", "2");
    /* a macro may expand into top-level definitions, even inside sequence */
    CHECK_RUN("(sequence (define-macro m (lambda () '(define z 7))) (m)) z", "7");
    /* expansion happens before the form runs, but after earlier forms */
    CHECK_RUN("(define-macro twice (lambda (e) `(list ,e ,e))) (twice (+ 1 2))", "(3 3)");
    CHECK_RUN_ERROR("(define-macro m 5)", ":wrong-type");
    CHECK_RUN_ERROR("(define-macro m (lambda () (error \"inside\"))) (m)", ":error");
    /* a macro name is not a value */
    CHECK_RUN_ERROR("(define-macro m (lambda () 1)) m", ":unbound-symbol");
}

/* ---- evaluation (§8.1, §8.4) ------------------------------------------------------------- */

static void t_lexical_scope(void) {
    CHECK_RUN("(define make-counter (lambda (start) (lambda (step) (+ start step))))"
              "(let ((from-ten (make-counter 10))) (list (from-ten 1) (from-ten 2)))",
              "(11 12)");
    CHECK_RUN("(define x 1) (define read-x (lambda () x)) (define x 2) (read-x)", "2");
    CHECK_RUN("((lambda (a b &rest more) (list a b more)) 1 2 3 4)", "(1 2 (3 4))");
    CHECK_RUN("((lambda (x) ((lambda (y) ((lambda (z) (list x y z)) 3)) 2)) 1)", "(1 2 3)");
    CHECK_RUN_ERROR("((lambda (x) x) 1 2)", ":wrong-arguments");
    CHECK_RUN_ERROR("((lambda (x y &rest z) x) 1)", ":wrong-arguments");
}

static void t_map_literal_order(void) {
    CHECK_OUTPUT("(import io) (define m {(sequence (display! \"a\") 1) (sequence (display! \"b\") 2)"
                 " (sequence (display! \"c\") 3) (sequence (display! \"d\") 4)})",
                 "abcd");
    CHECK_RUN("(define k 1) {k :a (+ k 1) :b}", "{1 :a 2 :b}");
}

static void t_tail_calls(void) {
    CHECK_RUN("(define loop (lambda (n) (if (= n 0) :done otherwise (loop (- n 1))))) (loop 1000000)", ":done");
    CHECK_RUN("(define loop (lambda (n) (sequence 1 (if (= n 0) :done otherwise (loop (- n 1))))))"
              "(loop 1000000)",
              ":done");
    CHECK_RUN("(define loop (lambda (n) (and #true (or #false (if (= n 0) :done otherwise (loop (- n 1)))))))"
              "(loop 1000000)",
              ":done");
    CHECK_RUN("(define even (lambda (n) (if (= n 0) #true otherwise (odd (- n 1)))))"
              "(define odd (lambda (n) (if (= n 0) #false otherwise (even (- n 1)))))"
              "(even 1000001)",
              "#false");
    CHECK_RUN("(define loop (lambda (n) (if (= n 0) :done otherwise (apply loop (list (- n 1))))))"
              "(loop 1000)",
              ":done");
}

typedef struct DeepCall {
    const char *source;
    LispRunResult result;
    char kind[64];
    int64_t value;
} DeepCall;

static void run_deep(void *argument) {
    DeepCall *call = argument;
    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&output), .stack_limit = lisp_large_stack_limit()};
    LispState *state = lisp_state_new(&options);
    Value *value = nullptr;
    LispReadError read_error;
    call->result = lisp_run_source(state, call->source, strlen(call->source), &value, &read_error);
    if (call->result == LISP_RUN_OK && value->kind == LISP_KIND_INTEGER) call->value = lisp_integer_value(value);
    if (call->result == LISP_RUN_ERROR) {
        snprintf(call->kind, sizeof call->kind, "%s", lisp_as_name(lisp_error_kind(state))->text);
    }
    lisp_release(value);
    lisp_state_free(state);
    lisp_buffer_free(&output);
}

static void t_deep_recursion_and_stack_overflow(void) {
    DeepCall deep = {.source = "(define depth (lambda (n) (if (= n 0) 0 otherwise (+ 1 (depth (- n 1))))))"
                               "(depth 100000)"};
    CHECK(lisp_run_on_large_stack(run_deep, &deep));
    CHECK_INT(deep.result, LISP_RUN_OK);
    CHECK_INT(deep.value, 100000);

    DeepCall endless = {.source = "(define forever (lambda (n) (+ 1 (forever n)))) (forever 0)"};
    CHECK(lisp_run_on_large_stack(run_deep, &endless));
    CHECK_INT(endless.result, LISP_RUN_ERROR);
    CHECK_STR(endless.kind, "stack-overflow");

    /* the default limit protects an ordinary thread too */
    CHECK_RUN_ERROR("(define forever (lambda (n) (+ 1 (forever n)))) (forever 0)", ":stack-overflow");
}

/* ---- primitives (§9) ------------------------------------------------------------------------ */

static void t_arithmetic_overflow(void) {
    CHECK_RUN_ERROR("(* -1 -9223372036854775808)", ":integer-overflow");
    CHECK_RUN_ERROR("(quotient -9223372036854775808 -1)", ":integer-overflow");
    CHECK_RUN("(remainder -9223372036854775808 -1)", "0");
    CHECK_RUN("(list (+ 9223372036854775806 1) (- -9223372036854775807 1))",
              "(9223372036854775807 -9223372036854775808)");
    CHECK_RUN("(list (quotient 7 -2) (remainder 7 -2) (remainder -7 -2))", "(-3 1 -1)");
    CHECK_RUN_ERROR("(< 1 2 \"3\")", ":wrong-type");
    CHECK_RUN_ERROR("(-)", ":wrong-arguments");
    CHECK_RUN_ERROR("(= 1)", ":wrong-arguments");
}

static void t_code_point_indexing(void) {
    CHECK_RUN("(list (count \"a\xCE\xBB\xF0\x9F\x98\x80\") (string-at \"a\xCE\xBB\xF0\x9F\x98\x80\" 2))",
              "(3 \"\xF0\x9F\x98\x80\")");
    CHECK_RUN("(substring \"a\xCE\xBB\xF0\x9F\x98\x80" "b\" 1 3)", "\"\xCE\xBB\xF0\x9F\x98\x80\"");
    CHECK_RUN("(list (substring \"abc\" 0 0) (substring \"abc\" 3 3) (substring \"abc\" 0 3))", "(\"\" \"\" \"abc\")");
    CHECK_RUN_ERROR("(string-at \"abc\" -1)", ":index-out-of-range");
    CHECK_RUN_ERROR("(substring \"abc\" 2 1)", ":index-out-of-range");
    CHECK_RUN_ERROR("(substring \"abc\" 0 4)", ":index-out-of-range");
}

static void t_other_primitives(void) {
    CHECK_RUN("(to-string (list \"a\" :b))", "\"(\\\"a\\\" :b)\"");
    CHECK_RUN("(list (string-to-number \"007\") (string-to-number \"-\") (string-to-number \"+5\"))", "(7 #void #void)");
    CHECK_RUN("(symbol-to-string (string-to-symbol \"a b\"))", "\"a b\"");
    CHECK_RUN("(list (not #false) (not ()) (not 0))", "(#true #false #false)");
    CHECK_RUN("(apply list 1 2 ())", "(1 2)");
    CHECK_RUN_ERROR("(apply + 1 2)", ":wrong-type");
    CHECK_RUN_ERROR("(apply 1 ())", ":not-a-function");
    CHECK_RUN_ERROR("(prepend 1 2)", ":wrong-type");
    CHECK_RUN_ERROR("(count 5)", ":wrong-type");
    CHECK_RUN_ERROR("(error :not-a-string)", ":wrong-type");
    CHECK_RUN("(catch (lambda () (raise {:kind :mine :message \"m\" :values ()})) (lambda (p) (get p :kind)))", ":mine");
    CHECK_RUN("(catch (lambda () (catch (lambda () (error \"a\")) (lambda (p) (raise p))))"
              " (lambda (p) (get p :message)))",
              "\"a\"");
}

/* ---- import (§8.9) ---------------------------------------------------------------------------- */

static void t_import(void) {
    CHECK_OUTPUT("(import io) (write! \"x\") (display! \"y\")", "\"x\"y");
    CHECK_OUTPUT("(import io a) (import io b) (a/display! 1) (b/display! 2)", "12");
    CHECK_RUN_ERROR("(import io a) (a/write! 1) (write! 2)", ":unbound-symbol");
    CHECK_RUN_ERROR("(import)", ":expansion-error");
    CHECK_RUN_ERROR("(import io :p)", ":expansion-error");
    CHECK_RUN_ERROR("(import list)", ":import-error");
}

const TestSuite eval_suite = {"eval", (const TestCase[]){
    {"special form shapes", t_special_form_shapes},
    {"derived forms", t_derived_forms},
    {"quasiquote", t_quasiquote},
    {"macros", t_macros},
    {"lexical scope", t_lexical_scope},
    {"map literals evaluate left to right", t_map_literal_order},
    {"tail calls in every tail position", t_tail_calls},
    {"deep recursion and stack overflow", t_deep_recursion_and_stack_overflow},
    {"arithmetic overflow", t_arithmetic_overflow},
    {"strings index code points", t_code_point_indexing},
    {"other primitives", t_other_primitives},
    {"import", t_import},
    {},
}};
