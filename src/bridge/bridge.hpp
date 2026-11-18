#pragma once

// The only code that knows about both the Transition Manager and Transition
// Tree. It fills in each component's optional hooks so they work together:
//
//  - Transition Tree's "New transition" creates it through the manager and
//    opens the manager on it; each rule's transitions get an Edit button that
//    opens the manager on that transition.
//  - The manager's header and removal prompt say which rules use a transition,
//    and its menus can list those rules in Transition Tree.
//  - Transitions created or renamed by the manager are followed by the rules.
//
// Neither component includes this file or each other's headers.
namespace Bridge {

void connect();
void disconnect();

} // namespace Bridge
