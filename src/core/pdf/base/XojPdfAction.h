/*
 * Xournal++
 *
 * PDF Action Abstraction Interface
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include "model/LinkDestination.h"  // for XojLinkDest


class XojPdfAction {
public:
    XojPdfAction();
    virtual ~XojPdfAction();

public:
    virtual XojLinkDest* getDestination() = 0;
    virtual std::string getTitle() = 0;

private:
};
