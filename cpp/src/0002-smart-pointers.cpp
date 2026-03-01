/* micrometrics : Smart Pointers
 *
 * Behaviour of all standard C++ smart pointer types and their idioms.
 *
 * Sections
 *   1  simple-creation    - construct unique_ptr, shared_ptr and weak_ptr
 *   2  double-ownership   - UB: two shared_ptr wrapping the same raw pointer
 *   3  move-semantics     - std::move with unique_ptr and shared_ptr
 *   4  shared-from-this   - enable_shared_from_this and safe self-shared_ptr
 *   5  ref-counters       - step-by-step use_count and weak ref-count changes
 *
 * Usage:
 *   ./0002-smart-pointers <section-number>
 *
 * Build:
 *   g++ -std=c++17 -O2 -o 0002-smart-pointers 0002-smart-pointers.cpp
 *
 * Debug:
 *   gdb ./0002-smart-pointers
 *
 * Copyright (c) 2026, Augusto Damasceno. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * See (https://github.com/augustodamasceno/micrometrics)
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>


struct Resource {
    std::string name;
    explicit Resource(std::string n) : name(std::move(n)) {
        std::cout << "  [+] Resource(" << name << ")\n";
    }
    ~Resource() {
        std::cout << "  [-] ~Resource(" << name << ")\n";
    }
    void greet() const {
        std::cout << "  Resource::greet()- " << name << "\n";
    }
};

// Node with enable_shared_from_this for section 4.
struct Node : std::enable_shared_from_this<Node> {
    std::string id;
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    explicit Node(std::string i) : id(std::move(i)) {
        std::cout << "  [+] Node(" << id << ")\n";
    }
    ~Node() {
        std::cout << "  [-] ~Node(" << id << ")\n";
    }
    void add_child(std::shared_ptr<Node> child) {
        child->parent = shared_from_this();
        children.push_back(std::move(child));
    }
    std::shared_ptr<Node> self() {
        return shared_from_this();
    }
};


// 1 ─ Simple creation of unique_ptr, shared_ptr and weak_ptr
void section_simple_creation() {
    std::cout << "\n--- unique_ptr ---\n";
    {
        // make_unique: preferred, single heap allocation
        auto u = std::make_unique<Resource>("unique");
        u->greet();
        std::cout << "  raw ptr via get() : " << u.get() << "\n";
    } // destructor called automatically here

    std::cout << "\n--- unique_ptr (array) ---\n";
    {
        auto arr = std::make_unique<int[]>(3);
        arr[0] = 10; arr[1] = 20; arr[2] = 30;
        for (int i = 0; i < 3; ++i)
            std::cout << "  arr[" << i << "] = " << arr[i] << "\n";
    }

    std::cout << "\n--- shared_ptr ---\n";
    {
        auto s1 = std::make_shared<Resource>("shared");
        std::cout << "  use_count = " << s1.use_count() << "\n"; // 1
        {
            auto s2 = s1;  // copy- shared ownership
            std::cout << "  use_count after copy = " << s1.use_count() << "\n"; // 2
            s2->greet();
        }
        std::cout << "  use_count after s2 scope = " << s1.use_count() << "\n"; // 1
    }

    std::cout << "\n--- weak_ptr ---\n";
    {
        std::weak_ptr<Resource> wp;
        {
            auto sp = std::make_shared<Resource>("observed");
            wp = sp;
            std::cout << "  expired() while owner alive : "
                      << std::boolalpha << wp.expired() << "\n"; // false
            if (auto locked = wp.lock())
                locked->greet();
        } // sp destroyed
        std::cout << "  expired() after owner gone  : " << wp.expired() << "\n"; // true
        std::cout << "  lock() returns null         : "
                  << (wp.lock() == nullptr) << "\n";
    }
}

// 2 ─ Undefined behaviour: two independent shared_ptr owning the same raw ptr
// WARNING: This deliberately causes a double-free / heap corruption.
//          The program will likely crash with a segfault or abort on exit.
void section_double_ownership() {
    std::cout << "\n--- two shared_ptr from the same raw pointer (UB) ---\n";
    std::cout << "  NOTE: this is undefined behaviour- double-free expected\n\n";

    Resource* raw = new Resource("double-owned");

    // Both sp1 and sp2 think they are the sole owner.
    // Each will try to delete raw when its ref-count reaches zero.
    std::shared_ptr<Resource> sp1(raw);
    std::shared_ptr<Resource> sp2(raw); // UB starts here

    std::cout << "  sp1 use_count : " << sp1.use_count() << "\n"; // 1 (not 2!)
    std::cout << "  sp2 use_count : " << sp2.use_count() << "\n"; // 1

    sp1->greet();
    sp2->greet();

    std::cout << "  leaving scope- first delete is fine, second is UB...\n";
    // sp2 destructs first (LIFO), deletes raw → ok
    // sp1 destructs next, deletes raw again → double-free / crash
}

// 3 ─ std::move with unique_ptr and shared_ptr
void section_move_semantics() {
    std::cout << "\n--- std::move with unique_ptr ---\n";
    {
        auto u1 = std::make_unique<Resource>("u-move");
        std::cout << "  u1 before move : " << u1.get() << "\n";

        auto u2 = std::move(u1);  // ownership transferred, u1 becomes null
        std::cout << "  u1 after  move : " << u1.get()
                  << " (null=" << (u1 == nullptr) << ")\n";
        std::cout << "  u2 after  move : " << u2.get() << "\n";
        u2->greet();
    }

    std::cout << "\n--- passing unique_ptr into a function (sink) ---\n";
    {
        auto consume = [](std::unique_ptr<Resource> r) {
            std::cout << "  function owns: ";
            r->greet();
        }; // r destroyed here
        auto u = std::make_unique<Resource>("u-sink");
        consume(std::move(u));
        std::cout << "  u is now null : " << (u == nullptr) << "\n";
    }

    std::cout << "\n--- std::move with shared_ptr (no ref-count bump) ---\n";
    {
        auto s1 = std::make_shared<Resource>("s-move");
        std::cout << "  use_count before move : " << s1.use_count() << "\n"; // 1

        auto s2 = std::move(s1); // moves ownership, no extra ref-count increment
        std::cout << "  s1 null after move    : " << (s1 == nullptr) << "\n";
        std::cout << "  s2 use_count          : " << s2.use_count() << "\n"; // still 1
        s2->greet();
    }

    std::cout << "\n--- move into a vector (unique_ptr) ---\n";
    {
        std::vector<std::unique_ptr<Resource>> vec;
        vec.push_back(std::make_unique<Resource>("v0"));
        vec.push_back(std::make_unique<Resource>("v1"));
        vec.push_back(std::make_unique<Resource>("v2"));
        for (auto& p : vec) p->greet();
    }
}

// 5 ─ Ref-counter and weak-counter step by step
void section_ref_counters() {
    // shared_ptr internal layout (conceptual):
    //   control block  →  [ strong_count | weak_count | deleter | allocator ]
    // use_count()  returns strong_count
    // weak_count is not directly exposed; we infer it below.
    //
    // Rules:
    //   shared_ptr copy   → strong++ (weak unchanged)
    //   shared_ptr move   → strong unchanged (ownership transferred)
    //   shared_ptr dtor   → strong--; if strong==0 → object destroyed
    //                        if strong==0 && weak==0 → control block freed
    //   weak_ptr copy     → weak++
    //   weak_ptr dtor     → weak--
    //   weak_ptr::lock()  → if strong>0: strong++ (returns shared_ptr)
    //                        else        strong unchanged (returns null)

    std::cout << "\n--- strong (use) count ---\n";
    {
        auto sp1 = std::make_shared<Resource>("rc");  // strong=1
        std::cout << "  after make_shared          strong=" << sp1.use_count() << "\n"; // 1

        auto sp2 = sp1;                                // strong=2
        std::cout << "  after copy  (sp2=sp1)      strong=" << sp1.use_count() << "\n"; // 2

        auto sp3 = sp1;                                // strong=3
        std::cout << "  after copy  (sp3=sp1)      strong=" << sp1.use_count() << "\n"; // 3

        sp2.reset();                                   // strong=2
        std::cout << "  after sp2.reset()          strong=" << sp1.use_count() << "\n"; // 2

        {
            auto sp4 = std::move(sp3);                 // strong=2 (move, not copy)
            std::cout << "  after move to sp4          strong=" << sp1.use_count() << "\n"; // 2
            std::cout << "  sp3 is null                : " << (sp3 == nullptr) << "\n";
        }                                              // sp4 dtor → strong=1
        std::cout << "  after sp4 scope            strong=" << sp1.use_count() << "\n"; // 1
    }                                                  // sp1 dtor → strong=0 → Resource freed

    std::cout << "\n--- weak count (inferred via expired / lock) ---\n";
    {
        std::weak_ptr<Resource> wp1, wp2, wp3;

        // We use a nested scope to control shared_ptr lifetime precisely.
        {
            auto sp = std::make_shared<Resource>("wc");  // strong=1, weak=0
            std::cout << "  after make_shared          strong=" << sp.use_count() << "\n";

            wp1 = sp;                                    // weak++ → weak=1
            std::cout << "  after wp1=sp               strong=" << sp.use_count()
                      << "  wp1.expired=" << wp1.expired() << "\n";

            wp2 = wp1;                                   // weak++ → weak=2
            std::cout << "  after wp2=wp1              strong=" << sp.use_count()
                      << "  wp2.expired=" << wp2.expired() << "\n";

            wp3 = sp;                                    // weak++ → weak=3
            std::cout << "  after wp3=sp               strong=" << sp.use_count()
                      << "  wp3.expired=" << wp3.expired() << "\n";

            // lock() increments strong transiently
            if (auto locked = wp1.lock()) {              // strong=2 inside
                std::cout << "  during wp1.lock()          strong=" << locked.use_count() << "\n";
            }                                            // locked dtor → strong=1
            std::cout << "  after lock scope           strong=" << sp.use_count() << "\n";

            wp2.reset();                                 // weak-- → weak=2
            std::cout << "  after wp2.reset()          wp2.expired=" << wp2.expired() << "\n";

        }  // sp dtor → strong=0 → Resource freed; weak still has wp1, wp3
           // control block survives until wp1 and wp3 are also gone

        std::cout << "  after sp scope             wp1.expired=" << wp1.expired()
                  << "  wp3.expired=" << wp3.expired() << "\n";
        std::cout << "  lock() on expired wp       null=" << (wp1.lock() == nullptr) << "\n";
    }  // wp1, wp3 dtors → weak=0 → control block freed

    std::cout << "\n--- shared_ptr from weak_ptr::lock() bumps strong ---\n";
    {
        auto sp  = std::make_shared<Resource>("lock-bump");
        std::weak_ptr<Resource> wp = sp;

        std::cout << "  strong before lock : " << sp.use_count() << "\n"; // 1
        {
            auto locked = wp.lock();                     // strong=2
            std::cout << "  strong during lock : " << sp.use_count() << "\n"; // 2
            std::cout << "  same ptr           : " << (sp.get() == locked.get()) << "\n";
        }                                                // locked dtor → strong=1
        std::cout << "  strong after lock  : " << sp.use_count() << "\n"; // 1
    }
}

// 4 ─ enable_shared_from_this
void section_shared_from_this() {
    std::cout << "\n--- shared_from_this: safe self shared_ptr ---\n";
    {
        // CORRECT: node is already managed by a shared_ptr before calling self()
        auto node = std::make_shared<Node>("root");
        auto self = node->self(); // shared_from_this- increments ref-count
        std::cout << "  use_count after self() : " << node.use_count() << "\n"; // 2
        std::cout << "  same object            : "
                  << (node.get() == self.get()) << "\n";
    }

    std::cout << "\n--- cycle-safe tree with weak_ptr parent links ---\n";
    {
        auto root  = std::make_shared<Node>("root");
        auto child = std::make_shared<Node>("child");
        auto grand = std::make_shared<Node>("grandchild");

        root->add_child(child);
        child->add_child(grand);

        std::cout << "  root  use_count : " << root.use_count()  << "\n"; // 1
        std::cout << "  child use_count : " << child.use_count() << "\n"; // 2 (root owns it)

        if (auto p = grand->parent.lock())
            std::cout << "  grand->parent id : " << p->id << "\n";
        if (auto p = child->parent.lock())
            std::cout << "  child->parent id : " << p->id << "\n";
    } // all nodes freed cleanly- no leak

    std::cout << "\n--- UB: shared_from_this on a stack object ---\n";
    std::cout << "  NOTE: calling shared_from_this() on an object not managed\n"
                 "        by any shared_ptr throws std::bad_weak_ptr (C++17).\n\n";
    {
        Node stack_node("stack");
        try {
            auto bad = stack_node.self(); // throws std::bad_weak_ptr
            (void)bad;
        } catch (const std::bad_weak_ptr& e) {
            std::cout << "  caught std::bad_weak_ptr: " << e.what() << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Help menu
// ---------------------------------------------------------------------------
void print_help(const char* prog) {
    std::cout << "\nUsage: " << prog << " <section>\n\n"
              << "Sections:\n"
              << "  1  simple-creation  - construct unique_ptr, shared_ptr, weak_ptr\n"
              << "  2  double-ownership - UB: two shared_ptr owning the same raw pointer\n"
              << "  3  move-semantics   - std::move with unique_ptr and shared_ptr\n"
              << "  4  shared-from-this - enable_shared_from_this and self shared_ptr\n"
              << "  5  ref-counters     - step-by-step strong and weak ref-count changes\n"
              << "\nExample:\n"
              << "  " << prog << " 1\n\n";
}

// ---------------------------------------------------------------------------
// main- dispatch
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    int section = std::atoi(argv[1]);

    switch (section) {
        case 1:
            std::cout << "   Section 1: simple-creation   \n";
            section_simple_creation();
            break;
        case 2:
            std::cout << "   Section 2: double-ownership (UB)   \n";
            section_double_ownership();
            break;
        case 3:
            std::cout << "   Section 3: move-semantics   \n";
            section_move_semantics();
            break;
        case 4:
            std::cout << "   Section 4: shared-from-this   \n";
            section_shared_from_this();
            break;
        case 5:
            std::cout << "   Section 5: ref-counters   \n";
            section_ref_counters();
            break;
        default:
            std::cout << "Unknown section: " << argv[1] << "\n";
            print_help(argv[0]);
            return 1;
    }

    std::cout << "\n   done   \n";
    return 0;
}
