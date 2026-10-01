# Classes & Inheritance

## Definition

Define a class with `class`. Single inheritance uses `inherits`.

```pycp
class Animal {
    func __initialize__(self, name) {
        self.name = name
    }
    func speak(self) {
        io.print(self.name + " makes a sound")
    }
}

class Dog inherits Animal {
    func speak(self) {
        io.print(self.name + " barks")
    }
}
```

- `self` is **not** a keyword; it is the conventional first parameter of methods. You
  may name it differently, but it must be the receiver.
- Methods are ordinary `func` definitions inside the class body.
- Attribute access on `self` uses `.`; assigning a new name creates an instance
  attribute.

## Constructor: `__initialize__`

`__initialize__` is the constructor, called when an instance is created. It is the
special method that receives `self` and any extra arguments:

```pycp
d = Dog("Rex")   // calls Dog.__initialize__(d, "Rex")
```

`__initialize__` is dispatched specially by the VM (it is not in the generic magic
thunk table). See [magic-methods.md](magic-methods.md).

## Inheritance & `super`

- A class may inherit from exactly one parent (`inherits Parent`).
- Override a method by redefining it in the subclass.
- Call a superclass method with `classtools.super` (note: `super` is a module
  function, **not** a keyword):

```pycp
class Dog inherits Animal {
    func speak(self) {
        classtools.super(self).speak()
        io.print("...and wags tail")
    }
}
```

## Instances

An instance is created by calling the class like a function. Instance attributes are
stored per-object; methods are resolved through the class and its ancestors.

```pycp
a = Animal("generic")
a.speak()
```

## Magic methods on classes

Operator overloading (`+`, `==`, indexing, etc.) and stringification are defined via
magic methods — see [magic-methods.md](magic-methods.md).

## Quirks

- No multiple inheritance.
- `this` is **not** available inside methods; use the `self` parameter. (`this` is the
  module function `moduletools.this`.)
- Classes are first-class values: they can be assigned to variables, stored in
  containers, and passed around.
