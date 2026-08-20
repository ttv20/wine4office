/*
 * Copyright 2026 Elkana Bardugo
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

function expect_throw(func, message) {
    var threw = false;
    try {
        func();
    } catch (e) {
        threw = true;
    }
    ok(threw, message);
}

function expect_no_throw(func, message) {
    var threw = false;
    try {
        func();
    } catch (e) {
        threw = true;
    }
    ok(!threw, message);
}

function test_constructor_and_observe_validation() {
    var observer = new MutationObserver(function() {});
    var text = document.createTextNode("text"), default_get_count = 0;
    var truthy = {};

    Object.defineProperty(truthy, "valueOf", {get: function() {
        default_get_count++;
        throw new Error("valueOf accessed");
    }});
    Object.defineProperty(truthy, "toString", {get: function() {
        default_get_count++;
        throw new Error("toString accessed");
    }});

    expect_throw(function() { new MutationObserver(null); }, "null callback rejected");
    expect_throw(function() { new MutationObserver({}); }, "non-callable callback rejected");
    expect_throw(function() { observer.observe(null, {}); }, "null target rejected");
    expect_throw(function() { observer.observe(text, null); }, "null options rejected");
    expect_throw(function() { observer.observe(text, {}); }, "empty options rejected");
    expect_throw(function() { observer.observe(text, {attributes: true}); }, "attributes rejected honestly");
    expect_throw(function() { observer.observe(text, {attributeOldValue: true}); }, "attributeOldValue true implies unsupported attributes");
    expect_throw(function() { observer.observe(text, {attributeOldValue: false}); }, "present attributeOldValue implies unsupported attributes");
    expect_throw(function() { observer.observe(text, {attributeFilter: []}); }, "attributeFilter implies unsupported attributes");
    observer.observe(text, {childList: true});
    observer.disconnect();
    expect_throw(function() { observer.observe(text, {characterDataOldValue: true}); }, "characterDataOldValue true rejected honestly");
    expect_no_throw(function() { observer.observe(text, {characterDataOldValue: false}); },
                    "false characterDataOldValue implies supported characterData");
    observer.disconnect();
    expect_no_throw(function() { observer.observe(text, {characterData: true, attributeFilter: undefined}); },
                    "undefined attributeFilter is treated as absent");
    observer.disconnect();
    expect_no_throw(function() { observer.observe(text, {characterData: truthy}); },
                    "object characterData option is truthy");
    ok(default_get_count === 0, "object option truthiness did not access default properties");
    observer.disconnect();
    observer.observe(text, {characterData: true, attributes: false, attributeOldValue: false,
                           characterDataOldValue: false, childList: false});
    observer.disconnect();
    next_test();
}

function test_take_records_and_fields() {
    var text = document.createTextNode("first"), observer;
    var array_constructor = window.Array, records, record, threw = false;

    observer = new MutationObserver(function() {
        ok(false, "takeRecords mutation was delivered early");
    });
    observer.observe(text, {characterData: true});
    text.data = "second";
    window.Array = function() { throw new Error("poisoned Array called"); };
    try {
        records = observer.takeRecords();
    } catch (e) {
        threw = true;
    }
    window.Array = array_constructor;
    ok(!threw, "takeRecords ignored poisoned window.Array");
    if (threw)
        records = [];
    ok(records instanceof Array, "takeRecords returned an intrinsic Array");
    ok(records.length === 1, "takeRecords returned one record");
    record = records[0];
    ok(record.type === "characterData", "record type");
    ok(record.target === text, "record target identity");
    ok(record.previousSibling === null, "previousSibling is null");
    ok(record.nextSibling === null, "nextSibling is null");
    ok(record.attributeName === null, "attributeName is null");
    ok(record.attributeNamespace === null, "attributeNamespace is null");
    ok(record.oldValue === null, "oldValue is null");
    ok(record.addedNodes.length === 0, "addedNodes is an empty NodeList");
    ok(record.removedNodes.length === 0, "removedNodes is an empty NodeList");
    record.type = "changed";
    record.target = null;
    delete record.oldValue;
    ok(record.type === "characterData", "record type is read-only");
    ok(record.target === text, "record target is read-only");
    ok(record.oldValue === null, "record oldValue is non-configurable");
    ok(observer.takeRecords().length === 0, "takeRecords drained the queue");
    observer.disconnect();
    next_test();
}

function test_subtree_and_disconnect() {
    var parent = document.createElement("div"), child = document.createElement("span");
    var text = document.createTextNode("first"), observer, records;

    child.appendChild(text);
    parent.appendChild(child);
    observer = new MutationObserver(function() {
        ok(false, "subtree mutation was delivered early");
    });
    observer.observe(parent, {characterData: true, subtree: true});
    text.data = "second";
    records = observer.takeRecords();
    ok(records.length === 1, "subtree mutation matched");
    ok(records[0].target === text, "subtree target identity");

    text.data = "third";
    observer.disconnect();
    ok(observer.takeRecords().length === 0, "disconnect cleared pending records");
    next_test();
}

function test_reobserve_replaces_options() {
    var parent = document.createElement("div"), child = document.createElement("span");
    var text = document.createTextNode("first"), direct = document.createElement("i");
    var observer, records;

    child.appendChild(text);
    parent.appendChild(child);
    observer = new MutationObserver(function() {
        ok(false, "re-observed mutation was delivered before takeRecords");
    });
    observer.observe(parent, {characterData: true, subtree: true});
    observer.observe(parent, {childList: true});
    text.data = "second";
    child.appendChild(document.createElement("b"));
    parent.appendChild(direct);
    records = observer.takeRecords();
    ok(records.length === 1 && records[0].type === "childList" && records[0].target === parent,
       "re-observe replaced characterData and subtree options");

    observer.observe(parent, {characterData: true, subtree: true});
    parent.appendChild(document.createElement("em"));
    text.data = "third";
    records = observer.takeRecords();
    ok(records.length === 1 && records[0].type === "characterData" && records[0].target === text,
       "second re-observe replaced childList options");
    observer.disconnect();
    next_test();
}

function test_childlist_records_and_snapshots() {
    var parent = document.createElement("div"), first = document.createElement("i");
    var second = document.createElement("b"), inserted = document.createElement("em");
    var batch_first = document.createElement("strong"), batch_second = document.createElement("small");
    var fragment = document.createDocumentFragment(), observer, records, record, snapshot;

    observer = new MutationObserver(function() {
        ok(false, "childList mutation was delivered before takeRecords");
    });
    observer.observe(parent, {childList: true});

    first.id = "snapshot-first";
    parent.appendChild(first);
    records = observer.takeRecords();
    ok(records.length === 1, "append produced one childList record");
    record = records[0];
    ok(record.type === "childList", "append record type");
    ok(record.target === parent, "append record target");
    ok(record.previousSibling === null, "append previousSibling is null");
    ok(record.nextSibling === null, "append nextSibling is null");
    ok(record.addedNodes.length === 1, "append addedNodes length");
    snapshot = record.addedNodes;
    ok(snapshot instanceof NodeList, "addedNodes is a NodeList");
    ok(snapshot === record.addedNodes, "addedNodes snapshot identity is stable");
    ok(snapshot[0] === first, "append addedNodes indexed access");
    ok(snapshot.item(0) === first, "append addedNodes.item(0)");
    ok(record.removedNodes instanceof NodeList, "removedNodes is a NodeList");
    ok(record.removedNodes.length === 0, "append removedNodes is empty");

    parent.appendChild(second);
    ok(observer.takeRecords().length === 1, "second append was queued");
    parent.insertBefore(inserted, second);
    records = observer.takeRecords();
    ok(records.length === 1, "insert-before produced one childList record");
    record = records[0];
    ok(record.addedNodes.length === 1 && record.addedNodes.item(0) === inserted,
       "insert-before added node snapshot");
    ok(record.previousSibling === first, "insert-before previousSibling");
    ok(record.nextSibling === second, "insert-before nextSibling");

    fragment.appendChild(batch_first);
    fragment.appendChild(batch_second);
    parent.appendChild(fragment);
    records = observer.takeRecords();
    ok(records.length === 1, "fragment append coalesced to one native childList record");
    record = records[0];
    ok(record.addedNodes.length === 2, "fragment append addedNodes length");
    ok(record.addedNodes.item(0) === batch_first, "fragment append first node");
    ok(record.addedNodes.item(1) === batch_second, "fragment append second node");
    ok(record.addedNodes.item(2) === null, "fragment append out-of-range item is null");
    ok(record.previousSibling === second, "fragment append previousSibling");
    ok(record.nextSibling === null, "fragment append nextSibling is null");

    parent.removeChild(inserted);
    records = observer.takeRecords();
    ok(records.length === 1, "removal produced one childList record");
    record = records[0];
    ok(record.addedNodes.length === 0, "removal addedNodes is empty");
    ok(record.removedNodes.length === 1, "removal removedNodes length");
    ok(record.removedNodes.item(0) === inserted, "removal removedNodes.item(0)");
    ok(record.previousSibling === first, "removal previousSibling");
    ok(record.nextSibling === second, "removal nextSibling");

    parent.removeChild(first);
    ok(observer.takeRecords().length === 1, "snapshot follow-up removal was queued");
    observer.disconnect();
    first = null;
    CollectGarbage();
    ok(snapshot.length === 1 && snapshot.item(0).id === "snapshot-first",
       "addedNodes snapshot survived disconnect and garbage collection");
    next_test();
}

function test_childlist_subtree_and_disconnect() {
    var root = document.createElement("div"), child = document.createElement("span");
    var grandchild = document.createElement("b"), after_disconnect = document.createElement("i");
    var observer, records;

    root.appendChild(child);
    observer = new MutationObserver(function() {
        ok(false, "subtree childList mutation was delivered before takeRecords");
    });
    observer.observe(root, {childList: true, subtree: true});
    child.appendChild(grandchild);
    records = observer.takeRecords();
    ok(records.length === 1, "subtree childList mutation matched");
    ok(records[0].target === child, "subtree childList target is container");
    ok(records[0].addedNodes.item(0) === grandchild, "subtree added node");

    child.removeChild(grandchild);
    ok(observer.takeRecords().length === 1, "subtree removal matched");
    observer.disconnect();
    child.appendChild(after_disconnect);
    ok(observer.takeRecords().length === 0, "disconnect cleared childList observation");
    next_test();
}

function test_detached_subtree_before_delivery() {
    var root = document.createElement("div"), child = document.createElement("span");
    var text = document.createTextNode("first"), observer, records;

    child.appendChild(text);
    root.appendChild(child);
    observer = new MutationObserver(function() {
        ok(false, "detached subtree mutations were delivered before takeRecords");
    });
    observer.observe(root, {childList: true, characterData: true, subtree: true});
    root.removeChild(child);
    text.data = "second";
    records = observer.takeRecords();
    ok(records.length === 2, "removal and detached descendant mutation were queued");
    ok(records[0].type === "childList" && records[0].removedNodes.item(0) === child,
       "detached subtree removal record");
    ok(records[1].type === "characterData" && records[1].target === text,
       "detached descendant mutation record");

    text.data = "third";
    records = observer.takeRecords();
    ok(records.length === 1 && records[0].target === text,
       "takeRecords preserves transient observation until the checkpoint");
    observer.disconnect();
    next_test();
}

function test_childlist_reentrant_delivery() {
    var parent = document.createElement("div"), first = document.createElement("i");
    var second = document.createElement("b"), observer, callback_count = 0, finished = false;
    var timeout = window.setTimeout(function() {
        if (finished)
            return;
        finished = true;
        ok(false, "childList MutationObserver callback timed out");
        next_test();
    }, 5000);

    observer = new MutationObserver(function(records, callback_observer) {
        callback_count++;
        ok(this === observer, "childList callback this is observer");
        ok(callback_observer === observer, "childList callback observer identity");
        ok(records.length === 1, "childList callback received one record");
        if (callback_count === 1) {
            ok(records[0].addedNodes.item(0) === first, "first reentrant record node");
            CollectGarbage();
            parent.appendChild(second);
            return;
        }
        ok(callback_count === 2, "reentrant childList mutation delivered later");
        ok(records[0].addedNodes.item(0) === second, "second reentrant record node");
        finished = true;
        window.clearTimeout(timeout);
        callback_observer.disconnect();
        next_test();
    });

    observer.observe(parent, {childList: true});
    CollectGarbage();
    parent.appendChild(first);
}

function test_reentrant_delivery_and_lifetime() {
    var text = document.createTextNode("first"), callback_count = 0, finished = false;
    var timeout = window.setTimeout(function() {
        if (finished)
            return;
        finished = true;
        ok(false, "MutationObserver callback timed out");
        next_test();
    }, 5000);
    var observer = new MutationObserver(function(records, callback_observer) {
        callback_count++;
        ok(this === observer, "callback this is observer");
        ok(records instanceof Array, "callback records is an Array");
        ok(callback_observer === observer, "callback observer identity");
        ok(records.length === 1, "callback received one record");
        if (callback_count === 1) {
            CollectGarbage();
            text.data = "third";
            return;
        }
        ok(callback_count === 2, "reentrant mutation delivered later");
        finished = true;
        window.clearTimeout(timeout);
        this.disconnect();
        next_test();
    });

    observer.observe(text, {characterData: true});
    CollectGarbage();
    text.data = "second";
}

var tests = [test_constructor_and_observe_validation,
             test_take_records_and_fields,
             test_reobserve_replaces_options,
             test_childlist_records_and_snapshots,
             test_childlist_subtree_and_disconnect,
             test_detached_subtree_before_delivery,
             test_childlist_reentrant_delivery,
             test_subtree_and_disconnect,
             test_reentrant_delivery_and_lifetime];
