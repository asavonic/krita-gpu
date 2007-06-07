/*
 *  Copyright (c) 2002 Patrick Julien <freak@codepimps.org>
 *  Copyright (c) 2005 Casper Boemann <cbr@boemann.dk>
 *  Copyright (c) 2006 Bart Coppens <kde@bartcoppens.be>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#ifndef KIS_MERGE_H_
#define KIS_MERGE_H_

#include <QRect>

#include "kis_types.h"
#include "kis_paint_device.h"
#include "kis_layer_visitor.h"
#include "kis_painter.h"
#include "kis_image.h"
#include "kis_layer.h"
#include "kis_group_layer.h"
#include "kis_adjustment_layer.h"
#include "kis_external_layer_iface.h"
#include "kis_paint_layer.h"
#include "kis_filter.h"
#include "kis_filter_configuration.h"
#include "kis_filter_registry.h"
#include "kis_selection.h"
#include "kis_transaction.h"
#include "kis_iterators_pixel.h"
#include "kis_clone_layer.h"

class KisMergeVisitor : public KisLayerVisitor {
public:
    /**
     * Don't even _think_ of creating a merge visitor without a projection; without a projection,
     * the adjustmentlayers won't work.
     */
    KisMergeVisitor(KisPaintDeviceSP projection, const QRect& rc) :
        KisLayerVisitor()
        {
            Q_ASSERT(projection);

            m_projection = projection;
            m_rc = rc;
        }

public:

    bool visit( KisExternalLayer * layer )
        {
//             kDebug(41010) << "Visiting on external layer " << layer->name() << ", visible: " << layer->visible() << ", extent: "
//                           << layer->extent() << ", paint rect: " << m_rc << endl;

            if (m_projection.isNull()) {
                return false;
            }
            if (!layer->visible())
                return true;

            layer->updateProjection( m_rc );
            KisPaintDeviceSP dev = layer->projection();
            if (!dev)
                return true;

            qint32 sx, sy, dx, dy, w, h;

            QRect rc = dev->extent() & m_rc;

            sx= rc.left();
            sy = rc.top();
            w = rc.width();
            h = rc.height();
            dx = sx;
            dy = sy;

            KisPainter gc(m_projection);
            gc.setChannelFlags( layer->channelFlags() );
            gc.bitBlt(dx, dy, layer->compositeOp() , dev, layer->opacity(), sx, sy, w, h);

            layer->setClean( rc );

            return true;
        }

    bool visit(KisPaintLayer *layer)
        {

            if (m_projection.isNull()) {
                return false;
            }

//             kDebug(41010) << "Visiting on paint layer " << layer->name() << ", visible: " << layer->visible()
//                           << ", temporary: " << layer->temporary() << ", extent: "
//                           << layer->extent() << ", paint rect: " << m_rc << endl;
            if (!layer->visible())
                return true;

            qint32 sx, sy, dx, dy, w, h;

            QRect rc = layer->paintDevice()->extent() & m_rc;

            // Indirect painting?
            KisPaintDeviceSP tempTarget = layer->temporaryTarget();
            if (tempTarget) {
                rc = (layer->paintDevice()->extent() | tempTarget->extent()) & m_rc;
            }

            sx = rc.left();
            sy = rc.top();
            w  = rc.width();
            h  = rc.height();
            dx = sx;
            dy = sy;

            KisPainter gc(m_projection);
            gc.setChannelFlags( layer->channelFlags() );

            KisPaintDeviceSP source = layer->paintDevice();//projection();

            if (tempTarget) {
                KisPaintDeviceSP temp = new KisPaintDevice(source->colorSpace());
                source = paintIndirect(source, temp, layer, sx, sy, dx, dy, w, h);
            }

            gc.bitBlt(dx, dy, layer->compositeOp(), source, layer->opacity(), sx, sy, w, h);

            layer->setClean( rc );

            return true;
        }

    bool visit(KisGroupLayer *layer)
        {
//             kDebug(41010) << "Visiting on group layer " << layer->name() << ", visible: " << layer->visible() << ", extent: "
//                           << layer->extent() << ", paint rect: " << m_rc << endl;


            if (m_projection.isNull()) {
                return false;
            }

            if (!layer->visible())
                return true;

            qint32 sx, sy, dx, dy, w, h;

            layer->updateProjection( m_rc );
            KisPaintDeviceSP dev = layer->projection();

            QRect rc = dev->extent() & m_rc;

            sx = rc.left();
            sy = rc.top();
            w  = rc.width();
            h  = rc.height();
            dx = sx;
            dy = sy;

            KisPainter gc(m_projection);
            gc.setChannelFlags( layer->channelFlags() );
            gc.bitBlt(dx, dy, layer->compositeOp(), dev, layer->opacity(), sx, sy, w, h);

            layer->setClean( rc );

            return true;
        }

    bool visit(KisAdjustmentLayer* layer)
        {
//             kDebug(41010) << "Visiting on adjustment layer " << layer->name() << ", visible: " << layer->visible() << ", extent: "
//                           << layer->extent() << ", paint rect: " << m_rc << endl;

            if (m_projection.isNull()) {
                return true;
            }

            if (!layer->visible())
                return true;

            KisPaintDeviceSP tempTarget = layer->temporaryTarget();
            if (tempTarget) {
                m_rc = (layer->extent() | tempTarget->extent()) & m_rc;
            }


            if (m_rc.width() == 0 || m_rc.height() == 0) // Don't even try
                return true;

            KisFilterConfiguration * cfg = layer->filter();
            if (!cfg) return false;


            KisFilterSP f = KisFilterRegistry::instance()->value( cfg->name() );
            if (!f) return false;

            // Possibly enlarge the rect that changed (like for convolution filters)
            // m_rc = f->enlargeRect(m_rc, cfg);

            KisSelectionSP selection = layer->selection();

            // Copy of the projection -- use the copy-on-write trick. XXX NO COPY ON WRITE YET =(
            //KisPaintDeviceSP tmp = new KisPaintDevice(*m_projection);
            KisPaintDeviceSP tmp((KisPaintDevice*)0);
            KisSelectionSP sel = selection;

            // If there's a selection, only keep the selected bits
            if (!selection.isNull()) {
                tmp = new KisPaintDevice(m_projection->colorSpace());

                KisPainter gc(tmp);
                QRect selectedRect = selection->selectedRect();
                selectedRect &= m_rc;

                if (selectedRect.width() == 0 || selectedRect.height() == 0) // Don't even try
                    return true;

                // Don't forget that we need to take into account the extended sourcing area as well
                //selectedRect = f->enlargeRect(selectedRect, cfg);

                tmp->setX(selection->getX());
                tmp->setY(selection->getY());

                // Indirect painting
                if (tempTarget) {
                    sel = new KisSelection();
                    sel = paintIndirect(selection.data(), sel, layer, m_rc.left(), m_rc.top(),
                                        m_rc.left(), m_rc.top(), m_rc.width(), m_rc.height());
                }

                gc.bitBlt(selectedRect.x(), selectedRect.y(), COMPOSITE_COPY, m_projection,
                          selectedRect.x(), selectedRect.y(),
                          selectedRect.width(), selectedRect.height());
                gc.end();
            } else {
                tmp = new KisPaintDevice(*m_projection);
            }

            // Some filters will require usage of oldRawData, which is not available without
            // a transaction!
            KisTransaction* cmd = new KisTransaction("", tmp);

            // Filter the temporary paint device -- remember, these are only the selected bits,
            // if there was a selection.
            f->process(tmp, m_rc, cfg);

            delete cmd;

            // Copy the filtered bits onto the projection
            KisPainter gc(m_projection);
            if (selection)
                gc.bltSelection(m_rc.left(), m_rc.top(),
                                COMPOSITE_OVER, tmp, sel, layer->opacity(),
                                m_rc.left(), m_rc.top(), m_rc.width(), m_rc.height());
            else
                gc.bitBlt(m_rc.left(), m_rc.top(),
                          COMPOSITE_OVER, tmp, layer->opacity(),
                          m_rc.left(), m_rc.top(), m_rc.width(), m_rc.height());
            gc.end();

            // Copy the finished projection onto the cache
            gc.begin(layer->cachedPaintDevice());
            gc.bitBlt(m_rc.left(), m_rc.top(),
                      COMPOSITE_COPY, m_projection, OPACITY_OPAQUE,
                      m_rc.left(), m_rc.top(), m_rc.width(), m_rc.height());

            layer->setClean( m_rc );

            return true;
        }


    bool visit( KisCloneLayer * layer )
        {
//             kDebug(41010) << "Visiting on clone layer " << layer->name() << ", visible: " << layer->visible() << ", extent: "
//                           << layer->extent() << ", paint rect: " << m_rc << endl;


            if (m_projection.isNull()) {
                return false;
            }

            if (!layer->visible())
                return true;

            qint32 sx, sy, dx, dy, w, h;

            layer->updateProjection( m_rc );
            KisPaintDeviceSP dev = layer->projection();

            QRect rc = dev->extent() & m_rc;

            sx = rc.left();
            sy = rc.top();
            w  = rc.width();
            h  = rc.height();
            dx = sx;
            dy = sy;

            KisPainter gc(m_projection);
            gc.setCompositeOp( layer->compositeOp() );
            gc.setOpacity( layer->opacity() );
            gc.setChannelFlags( layer->channelFlags() );

            gc.bitBlt(rc.topLeft(), dev, rc);

            layer->setClean( rc );

            return true;

        }

private:
    // Helper for the indirect painting
    template<class Target>
    KisSharedPtr<Target> paintIndirect(KisPaintDeviceSP source,
                                       KisSharedPtr<Target> target,
                                       KisIndirectPaintingSupport* layer,
                                       qint32 sx, qint32 sy, qint32 dx, qint32 dy,
                                       qint32 w, qint32 h) {
        KisPainter gc2(target.data());
        gc2.bitBlt(dx, dy, COMPOSITE_COPY, source,
                   OPACITY_OPAQUE, sx, sy, w, h);
        gc2.bitBlt(dx, dy, layer->temporaryCompositeOp(), layer->temporaryTarget(),
                   layer->temporaryOpacity(), sx, sy, w, h);
        gc2.end();
        return target;
    }
    KisPaintDeviceSP m_projection;
    QRect m_rc;
};

#endif // KIS_MERGE_H_

