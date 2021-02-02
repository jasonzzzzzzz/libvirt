/*
 * hyperv_network_driver.c: network driver functions for Microsoft Hyper-V hosts
 *
 * Copyright (C) 2020 Datto Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include "datatypes.h"
#include "viralloc.h"
#include "network_conf.h"
#include "hyperv_network_driver.h"
#include "hyperv_wmi.h"

#define VIR_FROM_THIS VIR_FROM_HYPERV

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Utility functions
 */

static virNetworkPtr
hypervMsvmVirtualSwitchToNetwork(virConnectPtr conn, Msvm_VirtualEthernetSwitch *virtualSwitch)
{
    unsigned char uuid[VIR_UUID_BUFLEN];

    if (virUUIDParse(virtualSwitch->data->Name, uuid) < 0) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       _("Could not parse UUID from string '%s'"),
                       virtualSwitch->data->Name);
        return NULL;
    }

    return virGetNetwork(conn, virtualSwitch->data->ElementName, uuid);
}


static virNetworkPtr
hypervNetworkLookup(virConnectPtr conn, const char *property, const char *value)
{
    hypervPrivate *priv = conn->privateData;
    g_auto(virBuffer) query = VIR_BUFFER_INITIALIZER;
    g_autoptr(Msvm_VirtualEthernetSwitch) virtualSwitch = NULL;

    virBufferAsprintf(&query, MSVM_VIRTUALETHERNETSWITCH_WQL_SELECT "WHERE %s", property);
    virBufferEscapeSQL(&query, " = '%s'", value);

    if (hypervGetWmiClass(Msvm_VirtualEthernetSwitch, &virtualSwitch) < 0)
        return NULL;

    if (!virtualSwitch) {
        virReportError(VIR_ERR_NO_NETWORK,
                       _("No network found with property '%s' = '%s'"), property, value);
        return NULL;
    }

    return hypervMsvmVirtualSwitchToNetwork(conn, virtualSwitch);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Exported API functions
 */

#define MATCH(FLAG) (flags & (FLAG))
static int
hypervConnectListAllNetworks(virConnectPtr conn,
                             virNetworkPtr **nets,
                             unsigned int flags)
{
    int ret = -1;
    hypervPrivate *priv = conn->privateData;
    size_t count = 0;
    size_t i;
    g_auto(virBuffer) query = { g_string_new(MSVM_VIRTUALETHERNETSWITCH_WQL_SELECT
                                             "WHERE HealthState = 5"), 0 };
    g_autoptr(Msvm_VirtualEthernetSwitch) switches = NULL;
    Msvm_VirtualEthernetSwitch *entry = NULL;

    virCheckFlags(VIR_CONNECT_LIST_NETWORKS_FILTERS_ALL, -1);

    /*
     * Hyper-V networks are always active, persistent, and
     * autostarted, so return zero elements in case we are asked
     * for networks different than that.
     */
    if (MATCH(VIR_CONNECT_LIST_NETWORKS_FILTERS_ACTIVE) &&
        !(MATCH(VIR_CONNECT_LIST_NETWORKS_ACTIVE)))
        return 0;
    if (MATCH(VIR_CONNECT_LIST_NETWORKS_FILTERS_PERSISTENT) &&
        !(MATCH(VIR_CONNECT_LIST_NETWORKS_PERSISTENT)))
        return 0;
    if (MATCH(VIR_CONNECT_LIST_NETWORKS_FILTERS_AUTOSTART) &&
        !(MATCH(VIR_CONNECT_LIST_NETWORKS_AUTOSTART)))
        return 0;

    if (hypervGetWmiClass(Msvm_VirtualEthernetSwitch, &switches) < 0)
        goto cleanup;

    for (entry = switches; entry; entry = entry->next) {
        if (nets) {
            virNetworkPtr net = hypervMsvmVirtualSwitchToNetwork(conn, entry);
            if (!net)
                goto cleanup;
            if (VIR_APPEND_ELEMENT(*nets, count, net) < 0)
                goto cleanup;
        } else {
            ++count;
        }
    }

    ret = count;

 cleanup:
    if (ret < 0 && nets && *nets) {
        for (i = 0; i < count; ++i)
            VIR_FREE((*nets)[i]);
        VIR_FREE(*nets);
    }

    return ret;
}
#undef MATCH


static int
hypervConnectNumOfNetworks(virConnectPtr conn)
{
    return hypervConnectListAllNetworks(conn, NULL, 0);
}


static virNetworkPtr
hypervNetworkLookupByUUID(virConnectPtr conn, const unsigned char *uuid)
{
    char uuid_string[VIR_UUID_STRING_BUFLEN];

    virUUIDFormat(uuid, uuid_string);

    return hypervNetworkLookup(conn, "Name", uuid_string);
}


static virNetworkPtr
hypervNetworkLookupByName(virConnectPtr conn, const char *name)
{
    return hypervNetworkLookup(conn, "ElementName", name);
}


virNetworkDriver hypervNetworkDriver = {
    .connectNumOfNetworks = hypervConnectNumOfNetworks, /* 7.1.0 */
    .connectListAllNetworks = hypervConnectListAllNetworks, /* 7.1.0 */
    .networkLookupByUUID = hypervNetworkLookupByUUID, /* 7.1.0 */
    .networkLookupByName = hypervNetworkLookupByName, /* 7.1.0 */
};
