#pragma once

#include <QMetaType>
#include <obs.hpp>

// OBS stores each transition in its main-window combo box as a
// QVariant<OBSSource> declared under the name "OBSSource". Qt 6 matches
// metatypes across libraries by that name, so declaring it the same way here
// lets the plugin read and add entries the frontend understands.
Q_DECLARE_METATYPE(OBSSource)
