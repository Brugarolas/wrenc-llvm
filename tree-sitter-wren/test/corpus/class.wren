==============
Method closures
==============

Fn.new {
    System.print("hello world")
}

Fn.new(1) {
    System.print("hello world")
}

-----

(source_file
    (function_call receiver: (identifier) name: (identifier)
        (stmt_block
            (function_call receiver: (identifier) name: (identifier) (string_literal))
        )
    )

    (function_call receiver: (identifier) name: (identifier) (number)
        (stmt_block
            (function_call receiver: (identifier) name: (identifier) (string_literal))
        )
    )
)

==============
Constructors
==============

class Cls {
    construct my_ctor {}
    construct my_ctor() {}
    construct my_ctor(a) {}
    construct my_ctor(a, b, c) {}
}

-----

(source_file
    (class_definition name: (identifier)
        (method name: (identifier) (stmt_block))
        (method name: (identifier) (arg_list) (stmt_block))
        (method name: (identifier) (arg_list (identifier)) (stmt_block))
        (method name: (identifier) (arg_list (identifier) (identifier) (identifier)) (stmt_block))
    )
)

==============
Static methods
==============

class Cls {
    static my_static {}
    static my_static() {}
    static my_static=(a) {}
}

-----

(source_file
    (class_definition name: (identifier)
        (method name: (identifier) (stmt_block))
        (method name: (identifier) (arg_list) (stmt_block))
        (method name: (identifier) (arg_list (identifier)) (stmt_block))
    )
)

==============
Foreign methods
==============

class Cls {
    foreign my_func
    foreign my_func()
    foreign my_func(a, b, c)
    foreign my_func=(a)

    foreign construct new
    foreign static my_func
}

-----

(source_file
    (class_definition name: (identifier)
        (foreign_method name: (identifier))
        (foreign_method name: (identifier) (arg_list))
        (foreign_method name: (identifier) (arg_list (identifier) (identifier) (identifier)))
        (foreign_method name: (identifier) (arg_list (identifier)))

        (foreign_method name: (identifier))
        (foreign_method name: (identifier))
    )
)

==============
Foreign class
==============

foreign class Cls {}

-----

(source_file
    (class_definition name: (identifier))
)
