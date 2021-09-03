/*
 * Copyright (c) 2021 Abit More, and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "../common/database_fixture.hpp"

#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>

#include <boost/test/unit_test.hpp>

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( bsrm_tests, database_fixture )

/// Tests scenarios that unable to have BSDM-related asset issuer permission or extensions before hardfork
BOOST_AUTO_TEST_CASE( hardfork_protection_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      uint16_t old_bitmask = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update;
      uint16_t new_bitmask = ASSET_ISSUER_PERMISSION_MASK;

      uint16_t bitflag = VALID_FLAGS_MASK & ~committee_fed_asset;

      vector<operation> ops;

      // Testing asset_create_operation
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = bitflag;
      acop.common_options.issuer_permissions = old_bitmask;
      acop.bitasset_opts = bitasset_options();
      acop.bitasset_opts->minimum_feeds = 3;

      trx.operations.clear();
      trx.operations.push_back( acop );

      {
         auto& op = trx.operations.front().get<asset_create_operation>();

         // Unable to set new permission bit
         op.common_options.issuer_permissions = new_bitmask;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.common_options.issuer_permissions = old_bitmask;

         // Unable to set new extensions in bitasset options
         op.bitasset_opts->extensions.value.black_swan_response_method = 0;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.bitasset_opts->extensions.value.black_swan_response_method = {};

         acop = op;
      }

      // Able to create asset without new data
      processed_transaction ptx = PUSH_TX(db, trx, ~0);
      const asset_object& samcoin = db.get<asset_object>(ptx.operation_results[0].get<object_id_type>());
      asset_id_type samcoin_id = samcoin.id;

      BOOST_CHECK_EQUAL( samcoin.options.market_fee_percent, 100 );
      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).options.minimum_feeds, 3 );

      // Able to propose the good operation
      propose( acop );

      // Testing asset_update_operation
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = samcoin_id;
      auop.new_options = samcoin_id(db).options;

      trx.operations.clear();
      trx.operations.push_back( auop );

      {
         auto& op = trx.operations.front().get<asset_update_operation>();
         op.new_options.market_fee_percent = 200;

         // Unable to set new permission bit
         op.new_options.issuer_permissions = new_bitmask;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.issuer_permissions = old_bitmask;

         auop = op;
      }

      // Able to update asset without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.options.market_fee_percent, 200 );

      // Able to propose the good operation
      propose( auop );

      // Testing asset_update_bitasset_operation
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = samcoin_id;
      aubop.new_options = samcoin_id(db).bitasset_data(db).options;

      trx.operations.clear();
      trx.operations.push_back( aubop );

      {
         auto& op = trx.operations.front().get<asset_update_bitasset_operation>();
         op.new_options.minimum_feeds = 1;

         // Unable to set new extensions
         op.new_options.extensions.value.black_swan_response_method = 1;
         BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
         ops.push_back( op );
         op.new_options.extensions.value.black_swan_response_method = {};

         aubop = op;
      }

      // Able to update bitasset without new data
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin.bitasset_data(db).options.minimum_feeds, 1 );

      // Able to propose the good operation
      propose( aubop );

      // Unable to propose the invalid operations
      for( const operation& op : ops )
         BOOST_CHECK_THROW( propose( op ), fc::exception );

      // Check what we have now
      idump( (samcoin) );
      idump( (samcoin.bitasset_data(db)) );

      generate_block();

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      // Now able to propose the operations that was invalid
      for( const operation& op : ops )
         propose( op );

      generate_block();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests scenarios about setting non-UIA issuer permission bits on an UIA
BOOST_AUTO_TEST_CASE( uia_issuer_permissions_update_test )
{
   try {

      // Proceeds to a recent hard fork
      generate_blocks( HARDFORK_LIQUIDITY_POOL_TIME );
      generate_block();
      set_expiration( db, trx );

      ACTORS((sam));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );

      uint16_t old_bitmask = ASSET_ISSUER_PERMISSION_MASK & ~disable_bsrm_update;
      uint16_t new_bitmask = ASSET_ISSUER_PERMISSION_MASK;
      uint16_t uiamask = UIA_ASSET_ISSUER_PERMISSION_MASK;

      uint16_t uiaflag = uiamask & ~disable_new_supply; // Allow creating new supply

      vector<operation> ops;

      asset_id_type samcoin_id = create_user_issued_asset( "SAMCOIN", sam_id(db), uiaflag ).id;

      // Testing asset_update_operation
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = samcoin_id;
      auop.new_options = samcoin_id(db).options;
      auop.new_options.issuer_permissions = old_bitmask & ~global_settle & ~disable_force_settle;

      trx.operations.clear();
      trx.operations.push_back( auop );

      // Able to update asset with non-UIA issuer permission bits
      PUSH_TX(db, trx, ~0);

      // Able to propose too
      propose( auop );

      // Issue some coin
      issue_uia( sam_id, asset( 1, samcoin_id ) );

      // Unable to unset the non-UIA "disable" issuer permission bits
      auto perms = samcoin_id(db).options.issuer_permissions;

      auop.new_options.issuer_permissions = perms & ~disable_icr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = perms & ~disable_mcr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = perms & ~disable_mssr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      // Still able to propose
      auop.new_options.issuer_permissions = new_bitmask;
      propose( auop );

      // But no longer able to update directly
      auop.new_options.issuer_permissions = uiamask | witness_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | committee_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_icr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mcr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mssr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unset the non-UIA bits in issuer permissions, should succeed
      auop.new_options.issuer_permissions = uiamask;
      trx.operations.clear();
      trx.operations.push_back( auop );

      PUSH_TX(db, trx, ~0);

      BOOST_CHECK_EQUAL( samcoin_id(db).options.issuer_permissions, uiamask );

      // Burn all supply
      reserve_asset( sam_id, asset( 1, samcoin_id ) );

      BOOST_CHECK_EQUAL( samcoin_id(db).dynamic_asset_data_id(db).current_supply.value, 0 );

      // Still unable to set the non-UIA bits in issuer permissions
      auop.new_options.issuer_permissions = uiamask | witness_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | committee_fed_asset;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_icr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mcr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_mssr_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      auop.new_options.issuer_permissions = uiamask | disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      generate_block();
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests what kind of assets can have BSRM-related flags / issuer permissions / extensions
BOOST_AUTO_TEST_CASE( asset_permissions_flags_extensions_test )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      // Unable to create a PM with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_prediction_market( "TESTPM", sam_id, 0, disable_bsrm_update ), fc::exception );

      // Unable to create a MPA with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_bitasset( "TESTBIT", sam_id, 0, disable_bsrm_update ), fc::exception );

      // Unable to create a UIA with the disable_bsrm_update bit in flags
      BOOST_CHECK_THROW( create_user_issued_asset( "TESTUIA", sam_id(db), disable_bsrm_update ), fc::exception );

      // create a PM with a zero market_fee_percent
      const asset_object& pm = create_prediction_market( "TESTPM", sam_id, 0, charge_market_fee );
      asset_id_type pm_id = pm.id;

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.id;

      // create a UIA with a zero market_fee_percent
      const asset_object& uia = create_user_issued_asset( "TESTUIA", sam_id(db), charge_market_fee );
      asset_id_type uia_id = uia.id;

      // Prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;

      // Unable to set disable_bsrm_update bit in flags for PM
      auop.asset_to_update = pm_id;
      auop.new_options = pm_id(db).options;
      auop.new_options.flags |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Unable to propose either
      BOOST_CHECK_THROW( propose( auop ), fc::exception );

      // Unable to set disable_bsrm_update bit in flags for MPA
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;
      auop.new_options.flags |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Unable to propose either
      BOOST_CHECK_THROW( propose( auop ), fc::exception );

      // Unable to set disable_bsrm_update bit in flags for UIA
      auop.asset_to_update = uia_id;
      auop.new_options = uia_id(db).options;
      auop.new_options.flags |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // Unable to propose either
      BOOST_CHECK_THROW( propose( auop ), fc::exception );

      // Unable to set disable_bsrm_update bit in issuer_permissions for PM
      auop.asset_to_update = pm_id;
      auop.new_options = pm_id(db).options;
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // But able to propose
      propose( auop );

      // Unable to set disable_bsrm_update bit in issuer_permissions for UIA
      auop.asset_to_update = uia_id;
      auop.new_options = uia_id(db).options;
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      // But able to propose
      propose( auop );

      // Unable to create a UIA with disable_bsrm_update permission bit
      asset_create_operation acop;
      acop.issuer = sam_id;
      acop.symbol = "SAMCOIN";
      acop.precision = 2;
      acop.common_options.core_exchange_rate = price(asset(1,asset_id_type(1)),asset(1));
      acop.common_options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      acop.common_options.market_fee_percent = 100;
      acop.common_options.flags = charge_market_fee;
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | disable_bsrm_update;

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Able to create UIA without disable_bsrm_update permission bit
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Unable to create a PM with disable_bsrm_update permission bit
      acop.symbol = "SAMPM";
      acop.precision = asset_id_type()(db).precision;
      acop.is_prediction_market = true;
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle | disable_bsrm_update;
      acop.bitasset_opts = bitasset_options();

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Unable to create a PM with BSRM in extensions
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle;
      acop.bitasset_opts->extensions.value.black_swan_response_method = 0;

      trx.operations.clear();
      trx.operations.push_back( acop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Unable to propose either
      BOOST_CHECK_THROW( propose( acop ), fc::exception );

      // Able to create PM with no disable_bsrm_update permission bit nor BSRM in extensions
      acop.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK | global_settle;
      acop.bitasset_opts->extensions.value.black_swan_response_method.reset();
      trx.operations.clear();
      trx.operations.push_back( acop );
      PUSH_TX(db, trx, ~0);

      // Unable to update PM to set BSRM
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = pm_id;
      aubop.new_options = pm_id(db).bitasset_data(db).options;
      aubop.new_options.extensions.value.black_swan_response_method = 1;

      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      // Able to propose
      propose( aubop );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

/// Tests whether asset owner has permission to update bsrm
BOOST_AUTO_TEST_CASE( asset_owner_permissions_update_bsrm )
{
   try {

      // Advance to core-2467 hard fork
      auto mi = db.get_global_properties().parameters.maintenance_interval;
      generate_blocks(HARDFORK_CORE_2467_TIME - mi);
      generate_blocks(db.get_dynamic_global_properties().next_maintenance_time);
      set_expiration( db, trx );

      ACTORS((sam)(feeder));

      auto init_amount = 10000000 * GRAPHENE_BLOCKCHAIN_PRECISION;
      fund( sam, asset(init_amount) );
      fund( feeder, asset(init_amount) );

      // create a MPA with a zero market_fee_percent
      const asset_object& mpa = create_bitasset( "TESTBIT", sam_id, 0, charge_market_fee );
      asset_id_type mpa_id = mpa.id;

      BOOST_CHECK( mpa_id(db).can_owner_update_bsrm() );

      BOOST_CHECK( !mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      using bsrm_type = bitasset_options::black_swan_response_type;
      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::global_settlement );

      // add a price feed publisher and publish a feed
      update_feed_producers( mpa_id, { feeder_id } );

      price_feed f;
      f.settlement_price = price( asset(1,mpa_id), asset(1) );
      f.core_exchange_rate = price( asset(1,mpa_id), asset(1) );
      f.maintenance_collateral_ratio = 1850;
      f.maximum_short_squeeze_ratio = 1250;

      uint16_t feed_icr = 1900;

      publish_feed( mpa_id, feeder_id, f, feed_icr );

      // Prepare for asset update
      asset_update_operation auop;
      auop.issuer = sam_id;
      auop.asset_to_update = mpa_id;
      auop.new_options = mpa_id(db).options;

      // disable owner's permission to update bsrm
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( !mpa_id(db).can_owner_update_bsrm() );

      // check that owner can not update bsrm
      asset_update_bitasset_operation aubop;
      aubop.issuer = sam_id;
      aubop.asset_to_update = mpa_id;
      aubop.new_options = mpa_id(db).bitasset_data(db).options;

      aubop.new_options.extensions.value.black_swan_response_method = 1;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      aubop.new_options.extensions.value.black_swan_response_method.reset();

      BOOST_CHECK( !mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      // enable owner's permission to update bsrm
      auop.new_options.issuer_permissions &= ~disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( mpa_id(db).can_owner_update_bsrm() );

      // check that owner can update bsrm
      aubop.new_options.extensions.value.black_swan_response_method = 1;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      BOOST_REQUIRE( mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      BOOST_CHECK_EQUAL( *mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method, 1u );
      BOOST_CHECK( mpa.bitasset_data(db).get_black_swan_response_method() == bsrm_type::no_settlement );

      // check bsrm' valid range
      aubop.new_options.extensions.value.black_swan_response_method = 4;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );
      aubop.new_options.extensions.value.black_swan_response_method = 1;

      // Sam borrow some
      borrow( sam, asset(1000, mpa_id), asset(2000) );

      // disable owner's permission to update bsrm
      auop.new_options.issuer_permissions |= disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      PUSH_TX(db, trx, ~0);

      BOOST_CHECK( !mpa_id(db).can_owner_update_bsrm() );

      // check that owner can not update bsrm
      aubop.new_options.extensions.value.black_swan_response_method = 0;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      aubop.new_options.extensions.value.black_swan_response_method.reset();
      trx.operations.clear();
      trx.operations.push_back( aubop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      aubop.new_options.extensions.value.black_swan_response_method = 1;

      // able to update other params that still has permission E.G. force_settlement_delay_sec
      aubop.new_options.force_settlement_delay_sec += 1;
      trx.operations.clear();
      trx.operations.push_back( aubop );
      PUSH_TX(db, trx, ~0);

      BOOST_REQUIRE_EQUAL( mpa_id(db).bitasset_data(db).options.force_settlement_delay_sec,
                           aubop.new_options.force_settlement_delay_sec );

      BOOST_REQUIRE( mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method.valid() );

      BOOST_CHECK_EQUAL( *mpa_id(db).bitasset_data(db).options.extensions.value.black_swan_response_method, 1u );

      // unable to enable the permission to update bsrm
      auop.new_options.issuer_permissions &= ~disable_bsrm_update;
      trx.operations.clear();
      trx.operations.push_back( auop );
      BOOST_CHECK_THROW( PUSH_TX(db, trx, ~0), fc::exception );

      BOOST_CHECK( !mpa_id(db).can_owner_update_bsrm() );

      generate_block();

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_SUITE_END()
