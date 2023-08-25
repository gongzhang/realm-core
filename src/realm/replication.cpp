/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include <realm/replication.hpp>
#include <realm/util/logger.hpp>

#include <realm/list.hpp>
#include <realm/path.hpp>
#include <iostream>

using namespace realm;
using namespace realm::util;

const char* Replication::history_type_name(int type)
{
    switch (type) {
        case hist_None:
            return "None";
        case hist_OutOfRealm:
            return "Local out of Realm";
        case hist_InRealm:
            return "Local in-Realm";
        case hist_SyncClient:
            return "SyncClient";
        case hist_SyncServer:
            return "SyncServer";
        default:
            return "Unknown";
    }
}

void Replication::initialize(DB&)
{
    // Nothing needs to be done here
}

void Replication::do_initiate_transact(Group&, version_type, bool)
{
    char* data = m_stream.get_data();
    size_t size = m_stream.get_size();
    m_encoder.set_buffer(data, data + size);
}

Replication::version_type Replication::prepare_commit(version_type orig_version)
{
    char* data = m_stream.get_data();
    size_t size = m_encoder.write_position() - data;
    version_type new_version = prepare_changeset(data, size, orig_version); // Throws
    return new_version;
}

void Replication::add_class(TableKey table_key, StringData name, Table::Type type)
{
    if (auto logger = get_logger()) {
        if (type == Table::Type::Embedded) {
            logger->log(util::Logger::Level::debug, "Add %1 class '%2'", type, name);
        }
        else {
            logger->log(util::Logger::Level::debug, "Add class '%1'", name);
        }
    }
    unselect_all();
    m_encoder.insert_group_level_table(table_key); // Throws
}

void Replication::add_class_with_primary_key(TableKey tk, StringData name, DataType pk_type, StringData pk_name, bool,
                                             Table::Type table_type)
{
    if (auto logger = get_logger()) {
        logger->log(util::Logger::Level::debug, "Add %4 class '%1' with primary key property '%2' of %3",
                    Group::table_name_to_class_name(name), pk_name, pk_type, table_type);
    }
    REALM_ASSERT(table_type != Table::Type::Embedded);
    unselect_all();
    m_encoder.insert_group_level_table(tk); // Throws
}

void Replication::erase_class(TableKey tk, StringData table_name, size_t)
{
    if (auto logger = get_logger()) {
        logger->log(util::Logger::Level::debug, "Remove class '%1'", Group::table_name_to_class_name(table_name));
    }
    unselect_all();
    m_encoder.erase_class(tk); // Throws
}


void Replication::insert_column(const Table* t, ColKey col_key, DataType type, StringData col_name,
                                Table* target_table)
{
    if (auto logger = get_logger()) {
        const char* collection_type = "";
        if (col_key.is_collection()) {
            if (col_key.is_list()) {
                collection_type = "list ";
            }
            else if (col_key.is_dictionary()) {
                collection_type = "dictionary ";
            }
            else {
                collection_type = "set ";
            }
        }
        if (target_table) {
            logger->log(util::Logger::Level::debug, "On class '%1': Add property '%2' %3linking '%4'",
                        t->get_class_name(), col_name, collection_type, target_table->get_class_name());
        }
        else {
            logger->log(util::Logger::Level::debug, "On class '%1': Add property '%2' %3of %4", t->get_class_name(),
                        col_name, collection_type, type);
        }
    }
    select_table(t);                  // Throws
    m_encoder.insert_column(col_key); // Throws
}

void Replication::erase_column(const Table* t, ColKey col_key)
{
    if (auto logger = get_logger()) {
        logger->log(util::Logger::Level::debug, "On class '%1': Remove property '%2' %3of %4", t->get_class_name(),
                    t->get_column_name(col_key));
    }
    select_table(t);                 // Throws
    m_encoder.erase_column(col_key); // Throws
}

void Replication::create_object(const Table* t, GlobalKey id)
{
    if (auto logger = get_logger()) {
        logger->log(util::Logger::Level::debug, "Create object '%1'", t->get_class_name());
    }
    select_table(t);                              // Throws
    m_encoder.create_object(id.get_local_key(0)); // Throws
}

void Replication::create_object_with_primary_key(const Table* t, ObjKey key, Mixed pk)
{
    if (auto logger = get_logger()) {
        logger->log(util::Logger::Level::debug, "Create object '%1' with primary key %2", t->get_class_name(), pk);
    }
    select_table(t);              // Throws
    m_encoder.create_object(key); // Throws
}

void Replication::remove_object(const Table* t, ObjKey key)
{
    if (auto logger = get_logger()) {
        if (t->is_embedded()) {
            logger->log(util::Logger::Level::debug, "Remove embedded object '%1'", t->get_class_name());
        }
        else if (t->get_primary_key_column()) {
            logger->log(util::Logger::Level::debug, "Remove object '%1' with primary key %2", t->get_class_name(),
                        t->get_primary_key(key));
        }
        else {
            logger->log(util::Logger::Level::debug, "Remove object '%1'", t->get_class_name());
        }
    }
    select_table(t);              // Throws
    m_encoder.remove_object(key); // Throws
}

inline void Replication::select_obj(ObjKey key)
{
    if (key != m_selected_obj) {
        if (auto logger = get_logger()) {
            if (logger->would_log(util::Logger::Level::debug)) {
                auto class_name = m_selected_table->get_class_name();
                if (m_selected_table->get_primary_key_column()) {
                    auto pk = m_selected_table->get_primary_key(key);
                    logger->log(util::Logger::Level::debug, "Mutating object '%1' with primary key %2", class_name, pk);                   
                }
                else {
                    auto obj = m_selected_table->get_object(key);
                    FullPath full_path = obj.get_path();
                    auto top_table = m_selected_table->get_parent_group()->get_table(full_path.top_table);
                    auto pk = top_table->get_primary_key(full_path.top_objkey);
                    auto prop_name = top_table->get_column_name(full_path.path_from_top[0].get_col_key());
                    full_path.path_from_top[0] = PathElement(prop_name);
                    logger->log(util::Logger::Level::debug, "Mutating object '%1' with path '%2'[%3]%4",
                                class_name, top_table->get_class_name(), pk, full_path.path_from_top);

                }
            }
        }
        m_selected_obj = key;
    }
}

void Replication::do_set(const Table* t, ColKey col_key, ObjKey key, _impl::Instruction variant)
{
    if (variant != _impl::Instruction::instr_SetDefault) {
        select_table(t); // Throws
        select_obj(key);
        m_encoder.modify_object(col_key, key); // Throws
    }
}

void Replication::set(const Table* t, ColKey col_key, ObjKey key, Mixed value, _impl::Instruction variant)
{
    do_set(t, col_key, key, variant); // Throws
    if (auto logger = get_logger()) {
        if (logger->would_log(util::Logger::Level::trace)) {
            logger->log(util::Logger::Level::trace, "   Set '%1' to %2", t->get_column_name(col_key), value);
        }
    }
}

void Replication::list_insert(const CollectionBase& list, size_t list_ndx, Mixed value, size_t)
{
    select_collection(list);                                     // Throws
    m_encoder.collection_insert(list.translate_index(list_ndx)); // Throws
    if (auto logger = get_logger()) {
        if (logger->would_log(util::Logger::Level::trace)) {
            auto path = list.get_short_path();
            auto prop_name = m_selected_table->get_column_name(path[0].get_col_key());
            path[0] = PathElement(prop_name);
            logger->log(util::Logger::Level::trace, "   Inserting '%1' in %2 at position %3", value, path, list_ndx);
        }
    }
}


void Replication::do_select_table(const Table* table)
{
    m_encoder.select_table(table->get_key()); // Throws
    m_selected_table = table;
    m_selected_list = CollectionId();
    m_selected_obj = ObjKey();
}

void Replication::do_select_collection(const CollectionBase& list)
{
    select_table(list.get_table().unchecked_ptr());
    ColKey col_key = list.get_col_key();
    ObjKey key = list.get_owner_key();
    auto path = list.get_stable_path();

    select_obj(key);

    m_encoder.select_collection(col_key, key, path); // Throws
    m_selected_list = CollectionId(list.get_table()->get_key(), key, std::move(path));
}

void Replication::list_clear(const CollectionBase& list)
{
    select_collection(list);           // Throws
    m_encoder.collection_clear(list.size()); // Throws
}

void Replication::link_list_nullify(const Lst<ObjKey>& list, size_t link_ndx)
{
    select_collection(list);
    m_encoder.collection_erase(link_ndx);
}

void Replication::dictionary_insert(const CollectionBase& dict, size_t ndx, Mixed, Mixed)
{
    select_collection(dict);
    m_encoder.collection_insert(ndx);
}

void Replication::dictionary_set(const CollectionBase& dict, size_t ndx, Mixed, Mixed)
{
    select_collection(dict);
    m_encoder.collection_set(ndx);
}

void Replication::dictionary_erase(const CollectionBase& dict, size_t ndx, Mixed)
{
    select_collection(dict);
    m_encoder.collection_erase(ndx);
}

void Replication::dictionary_clear(const CollectionBase& dict)
{
    select_collection(dict);
    m_encoder.collection_clear(dict.size());
}
