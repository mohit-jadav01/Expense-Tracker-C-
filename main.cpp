#include "application.hpp"

// The executable delegates its lifetime to one application object.
int main() {
    MoneyMapApp application;
    return application.run();
}
