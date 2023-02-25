==============
Large basic test
==============

// Single-line comments.

class A {
    my_method(arg, a, b, c) {
        if (arg == 123) {
            return null
        }
        return arg + 1
    }

    setter=(value) {
        this.abc = 123
        System.print(1234, value.hello)
    }
}

-----

(source_file
    (comment)
    (class_definition name: (identifier)
        (method name: (identifier)
            (arg_list
                (identifier) (identifier) (identifier) (identifier)
            )
            (stmt_block
                (stmt_if
                    condition: (infix_call (identifier) (number))
                    (stmt_block (stmt_return (null_literal)))
                )
                (stmt_return (infix_call (identifier) (number)))
            )
        )
        (method name: (identifier)
            (arg_list (identifier))
            (stmt_block
                (function_call receiver: (identifier) (identifier) (number))
                (function_call receiver: (identifier) (identifier) (number) (function_call receiver: (identifier) (identifier)))
            )
        )
    )
)

==============
Block end-of-line matching
==============

if (true) {
    return null
}

-----

(source_file
    (stmt_if (true_literal) (stmt_block
        (stmt_return (null_literal))
    ))
)

==============
Comments
==============

// This is a comment
// This is also a comment

/* This is a
m
u
l
t
i
-line comment.
*/

-----

(source_file
    (comment)
    (comment)
    (block_comment)
)

==============
Nested multi-line comments
==============

/* Outer

outer /* inner */ outer

End of outer */

-----

(source_file
    (block_comment (block_comment))
)

==============
Empty
==============

-----

(source_file)

==============
Only one comment
==============

// Hello

-----

(source_file (comment))

==============
Binary operators
==============

1 + 2
1 / 2
1 .. 2
1 << 2
1 & 2
1 ^ 2
1 | 2
1 < 2
1 is 2
1 == 2
1 && 2
1 || 2

-----

(source_file
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
    (infix_call (number) (number))
)
