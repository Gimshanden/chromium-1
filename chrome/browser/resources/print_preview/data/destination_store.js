// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.exportPath('print_preview');

/**
 * Printer search statuses used by the destination store.
 * @enum {string}
 */
print_preview.DestinationStorePrinterSearchStatus = {
  START: 'start',
  SEARCHING: 'searching',
  DONE: 'done'
};

/**
 * Enumeration of possible destination errors.
 * @enum {number}
 */
print_preview.DestinationErrorType = {
  INVALID: 0,
  UNSUPPORTED: 1,
  // <if expr="chromeos">
  NO_DESTINATIONS: 2,
  // </if>
};

cr.define('print_preview', function() {
  'use strict';
  /**
   * Localizes printer capabilities.
   * @param {!print_preview.Cdd} capabilities Printer capabilities to
   *     localize.
   * @return {!print_preview.Cdd} Localized capabilities.
   */
  const localizeCapabilities = function(capabilities) {
    if (!capabilities.printer) {
      return capabilities;
    }

    const mediaSize = capabilities.printer.media_size;
    if (!mediaSize) {
      return capabilities;
    }

    for (let i = 0, media; (media = mediaSize.option[i]); i++) {
      // No need to patch capabilities with localized names provided.
      if (!media.custom_display_name_localized) {
        media.custom_display_name = media.custom_display_name ||
            DestinationStore.MEDIA_DISPLAY_NAMES_[media.name] || media.name;
      }
    }
    return capabilities;
  };

  /**
   * Compare two media sizes by their names.
   * @param {!Object} a Media to compare.
   * @param {!Object} b Media to compare.
   * @return {number} 1 if a > b, -1 if a < b, or 0 if a == b.
   */
  const compareMediaNames = function(a, b) {
    const nameA = a.custom_display_name_localized || a.custom_display_name;
    const nameB = b.custom_display_name_localized || b.custom_display_name;
    return nameA == nameB ? 0 : (nameA > nameB ? 1 : -1);
  };

  /**
   * Sort printer media sizes.
   * @param {!print_preview.Cdd} capabilities Printer capabilities to
   * localize.
   * @return {!print_preview.Cdd} Localized capabilities.
   * @private
   */
  const sortMediaSizes = function(capabilities) {
    if (!capabilities.printer) {
      return capabilities;
    }

    const mediaSize = capabilities.printer.media_size;
    if (!mediaSize) {
      return capabilities;
    }

    // For the standard sizes, separate into categories, as seen in the Cloud
    // Print CDD guide:
    // - North American
    // - Chinese
    // - ISO
    // - Japanese
    // - Other metric
    // Otherwise, assume they are custom sizes.
    const categoryStandardNA = [];
    const categoryStandardCN = [];
    const categoryStandardISO = [];
    const categoryStandardJP = [];
    const categoryStandardMisc = [];
    const categoryCustom = [];
    for (let i = 0, media; (media = mediaSize.option[i]); i++) {
      const name = media.name || 'CUSTOM';
      let category;
      if (name.startsWith('NA_')) {
        category = categoryStandardNA;
      } else if (
          name.startsWith('PRC_') || name.startsWith('ROC_') ||
          name == 'OM_DAI_PA_KAI' || name == 'OM_JUURO_KU_KAI' ||
          name == 'OM_PA_KAI') {
        category = categoryStandardCN;
      } else if (name.startsWith('ISO_')) {
        category = categoryStandardISO;
      } else if (name.startsWith('JIS_') || name.startsWith('JPN_')) {
        category = categoryStandardJP;
      } else if (name.startsWith('OM_')) {
        category = categoryStandardMisc;
      } else {
        assert(name == 'CUSTOM', 'Unknown media size. Assuming custom');
        category = categoryCustom;
      }
      category.push(media);
    }

    // For each category, sort by name.
    categoryStandardNA.sort(compareMediaNames);
    categoryStandardCN.sort(compareMediaNames);
    categoryStandardISO.sort(compareMediaNames);
    categoryStandardJP.sort(compareMediaNames);
    categoryStandardMisc.sort(compareMediaNames);
    categoryCustom.sort(compareMediaNames);

    // Then put it all back together.
    mediaSize.option = categoryStandardNA;
    mediaSize.option.push(
        ...categoryStandardCN, ...categoryStandardISO, ...categoryStandardJP,
        ...categoryStandardMisc, ...categoryCustom);
    return capabilities;
  };


  class DestinationStore extends cr.EventTarget {
    /**
     * A data store that stores destinations and dispatches events when the
     * data store changes.
     * @param {function(string, !Function):void} addListenerCallback Function
     *     to call to add Web UI listeners in DestinationStore constructor.
     */
    constructor(addListenerCallback) {
      super();

      /**
       * Currently active user.
       * @private {string}
       */
      this.activeUser_ = '';

      /**
       * Whether the destination store will auto select the destination that
       * matches this set of parameters.
       * @private {print_preview.DestinationMatch}
       */
      this.autoSelectMatchingDestination_ = null;

      /**
       * ID of a timeout after the initial destination ID is set. If no inserted
       * destination matches the initial destination ID after the specified
       * timeout, the first destination in the store will be automatically
       * selected.
       * @private {?number}
       */
      this.autoSelectTimeout_ = null;

      /**
       * Used to fetch cloud-based print destinations.
       * @private {cloudprint.CloudPrintInterface}
       */
      this.cloudPrintInterface_ = null;

      /**
       * Cache used for constant lookup of destinations by key.
       * @private {!Map<string, !print_preview.Destination>}
       */
      this.destinationMap_ = new Map();

      /**
       * Internal backing store for the data store.
       * @private {!Array<!print_preview.Destination>}
       */
      this.destinations_ = [];

      /**
       * Whether a search for destinations is in progress for each type of
       * printer.
       * @private {!Map<!print_preview.PrinterType,
       *                !print_preview.DestinationStorePrinterSearchStatus>}
       */
      this.destinationSearchStatus_ = new Map([
        [
          print_preview.PrinterType.EXTENSION_PRINTER,
          print_preview.DestinationStorePrinterSearchStatus.START
        ],
        [
          print_preview.PrinterType.PRIVET_PRINTER,
          print_preview.DestinationStorePrinterSearchStatus.START
        ],
        [
          print_preview.PrinterType.LOCAL_PRINTER,
          print_preview.DestinationStorePrinterSearchStatus.START
        ],
      ]);

      /** @private {!Set<string>} */
      this.inFlightCloudPrintRequests_ = new Set();

      /**
       * Maps user account to the list of origins for which destinations are
       * already loaded.
       * @private {!Map<string, !Array<!print_preview.DestinationOrigin>>}
       */
      this.loadedCloudOrigins_ = new Map();

      /**
       * Used to track metrics.
       * @private {!print_preview.MetricsContext}
       */
      this.metrics_ = print_preview.MetricsContext.destinationSearch();

      /**
       * Used to fetch local print destinations.
       * @private {!print_preview.NativeLayer}
       */
      this.nativeLayer_ = print_preview.NativeLayer.getInstance();

      /**
       * Whether PDF printer is enabled. It's disabled, for example, in App
       * Kiosk mode.
       * @private {boolean}
       */
      this.pdfPrinterEnabled_ = false;

      /**
       * Local destinations are CROS destinations on ChromeOS because they
       * require extra setup.
       * @private {!print_preview.DestinationOrigin}
       */
      this.platformOrigin_ = cr.isChromeOS ?
          print_preview.DestinationOrigin.CROS :
          print_preview.DestinationOrigin.LOCAL;

      /**
       * Currently selected destination.
       * @private {print_preview.Destination}
       */
      this.selectedDestination_ = null;

      /**
       * Whether to select the first printer that is found. Used when
       * pdfPrinterEnabled_ is false.
       * @private {boolean}
       */
      this.selectFirstDestination_ = false;

      /**
       * ID of the system default destination.
       * @private {string}
       */
      this.systemDefaultDestinationId_ = '';

      /**
       * Event tracker used to track event listeners of the destination store.
       * @private {!EventTracker}
       */
      this.tracker_ = new EventTracker();

      /**
       * Whether to default to the system default printer instead of the most
       * recent destination.
       * @private {boolean}
       */
      this.useSystemDefaultAsDefault_ =
          loadTimeData.getBoolean('useSystemDefaultPrinter');

      addListenerCallback('printers-added', this.onPrintersAdded_.bind(this));
      addListenerCallback(
          'user-accounts-updated', this.onDestinationsReload.bind(this));
    }

    /**
     * @param {?string=} opt_account Account to filter destinations by. When
     *     null or omitted, all destinations are returned.
     * @return {!Array<!print_preview.Destination>} List of destinations
     *     accessible by the {@code account}.
     */
    destinations(opt_account) {
      if (opt_account) {
        return this.destinations_.filter(function(destination) {
          return !destination.account || destination.account == opt_account;
        });
      }
      return this.destinations_.slice(0);
    }

    /**
     * @return {boolean} Whether a search for print destinations is in progress.
     */
    get isPrintDestinationSearchInProgress() {
      const isLocalDestinationSearchInProgress =
          Array.from(this.destinationSearchStatus_.values())
              .some(
                  el => el ===
                      print_preview.DestinationStorePrinterSearchStatus
                          .SEARCHING);
      if (isLocalDestinationSearchInProgress) {
        return true;
      }

      const isCloudDestinationSearchInProgress = !!this.cloudPrintInterface_ &&
          this.cloudPrintInterface_.isCloudDestinationSearchInProgress();
      return isCloudDestinationSearchInProgress;
    }

    /**
     * @return {print_preview.Destination} The currently selected destination or
     *     {@code null} if none is selected.
     */
    get selectedDestination() {
      return this.selectedDestination_;
    }

    /**
     * @param {(?print_preview.Destination |
     *          ?print_preview.RecentDestination)} destination
     * @return {boolean} Whether the destination is valid.
     * @private
     */
    isDestinationValid_(destination) {
      return !!destination && !!destination.id && !!destination.origin;
    }

    /**
     * Initializes the destination store. Sets the initially selected
     * destination. If any inserted destinations match this ID, that destination
     * will be automatically selected.
     * @param {boolean} isInAppKioskMode Whether the print preview is in App
     *     Kiosk mode.
     * @param {string} systemDefaultDestinationId ID of the system default
     *     destination.
     * @param {?string} serializedDefaultDestinationSelectionRulesStr Serialized
     *     default destination selection rules.
     * @param {!Array<!print_preview.RecentDestination>}
     *     recentDestinations The recent print destinations.
     */
    init(
        isInAppKioskMode, systemDefaultDestinationId,
        serializedDefaultDestinationSelectionRulesStr, recentDestinations) {
      this.pdfPrinterEnabled_ = !isInAppKioskMode;
      this.systemDefaultDestinationId_ = systemDefaultDestinationId;
      this.createLocalPdfPrintDestination_();

      // System default printer policy takes priority.
      if (this.useSystemDefaultAsDefault_ &&
          this.selectSystemDefaultDestination_()) {
        return;
      }

      // Run through the destinations forward. As soon as we find a destination,
      // select or try to fetch it and return.
      for (const destination of recentDestinations) {
        const candidate = this.destinationMap_.get(
            print_preview.createRecentDestinationKey(destination));
        if (candidate) {
          this.selectDestination(candidate);
          return;
        }
        if (this.fetchPreselectedDestination_(destination)) {
          return;
        }
      }

      // Try the default destination rules, if they exist.
      const destinationMatch = this.convertToDestinationMatch_(
          serializedDefaultDestinationSelectionRulesStr);
      if (destinationMatch) {
        this.fetchMatchingDestination_(destinationMatch);
        this.startAutoSelectTimeout_();
        return;
      }

      // Try the system default.
      if (this.selectSystemDefaultDestination_()) {
        return;
      }

      // Fallback to Save as PDF, or the first printer to load (if in kiosk
      // mode).
      this.selectFinalFallbackDestination_();
    }

    /**
     * @return {boolean} Whether the system default printer was either found in
     *     the store or fetch was started successfully.
     */
    selectSystemDefaultDestination_() {
      if (this.systemDefaultDestinationId_ == '') {
        return false;
      }

      const serializedSystemDefault = {
        id: this.systemDefaultDestinationId_,
        origin: this.systemDefaultDestinationId_ ==
                print_preview.Destination.GooglePromotedId.SAVE_AS_PDF ?
            print_preview.DestinationOrigin.LOCAL :
            this.platformOrigin_,
        account: '',
        capabilities: null,
        displayName: '',
        extensionId: '',
        extensionName: '',
      };

      const systemDefaultCandidate = this.destinationMap_.get(
          print_preview.createRecentDestinationKey(serializedSystemDefault));
      if (systemDefaultCandidate) {
        this.selectDestination(systemDefaultCandidate);
        return true;
      }

      return this.fetchPreselectedDestination_(serializedSystemDefault);
    }

    /** Removes all events being tracked from the tracker. */
    resetTracker() {
      this.tracker_.removeAll();
    }

    /**
     * Attempts to fetch capabilities of the destination identified by
     * |serializedDestination|.
     * @param {!print_preview.RecentDestination} serializedDestination
     * @return {boolean} Whether capabilities fetch was successfully started.
     * @private
     */
    fetchPreselectedDestination_(serializedDestination) {
      const key =
          print_preview.createRecentDestinationKey(serializedDestination);
      if (this.inFlightCloudPrintRequests_.has(key)) {
        // Don't send another request if we are already fetching this
        // destination.
        return true;
      }

      const id = serializedDestination.id;
      const origin = serializedDestination.origin;
      this.autoSelectMatchingDestination_ =
          this.createExactDestinationMatch_(origin, id);

      let error = false;
      const type = print_preview.originToType(origin);
      switch (type) {
        case print_preview.PrinterType.LOCAL_PRINTER:
          this.nativeLayer_.getPrinterCapabilities(id, type).then(
              this.onCapabilitiesSet_.bind(this, origin, id),
              this.onGetCapabilitiesFail_.bind(this, origin, id));
          break;
        case print_preview.PrinterType.PRIVET_PRINTER:
        case print_preview.PrinterType.EXTENSION_PRINTER:
          // TODO(noamsml): Resolve a specific printer instead of listing all
          // privet or extension printers in this case.
          this.startLoadDestinations_(type);

          // Create a fake selectedDestination_ that is not actually in the
          // destination store. When the real destination is created, this
          // destination will be overwritten.
          const params =
              (origin === print_preview.DestinationOrigin.PRIVET) ? {} : {
                description: '',
                extensionId: serializedDestination.extensionId,
                extensionName: serializedDestination.extensionName,
                provisionalType: print_preview.DestinationProvisionalType.NONE
              };
          this.selectedDestination_ = new print_preview.Destination(
              id, print_preview.DestinationType.LOCAL, origin,
              serializedDestination.displayName,
              print_preview.DestinationConnectionStatus.ONLINE, params);

          if (serializedDestination.capabilities) {
            this.selectedDestination_.capabilities =
                serializedDestination.capabilities;
            this.dispatchEvent(
                new CustomEvent(DestinationStore.EventType
                                    .SELECTED_DESTINATION_CAPABILITIES_READY));
          }
          break;
        case print_preview.PrinterType.CLOUD_PRINTER:
          if (this.cloudPrintInterface_) {
            this.inFlightCloudPrintRequests_.add(key);
            this.cloudPrintInterface_.printer(
                id, origin, serializedDestination.account);
          } else {
            // No cloud print interface.
            error = true;
          }
          break;
        default:
          // Unknown type.
          error = true;
      }

      if (!error) {
        this.startAutoSelectTimeout_();
      }
      return !error;
    }

    /**
     * Attempts to find a destination matching the provided rules.
     * @param {!print_preview.DestinationMatch} destinationMatch Rules to match.
     * @private
     */
    fetchMatchingDestination_(destinationMatch) {
      this.autoSelectMatchingDestination_ = destinationMatch;
      const types = destinationMatch.getTypes();
      types.forEach(type => {
        if (type != print_preview.PrinterType.CLOUD_PRINTER) {
          // Local, extension, or privet printer
          this.startLoadDestinations_(type);
        } else if (print_preview.CloudOrigins.some(origin => {
                     return destinationMatch.matchOrigin(origin);
                   })) {
          this.startLoadCloudDestinations();
        }
      });
    }

    /**
     * @param {?string} serializedDefaultDestinationSelectionRulesStr Serialized
     *     default destination selection rules.
     * @return {?print_preview.DestinationMatch} Creates rules matching
     *     previously selected destination.
     * @private
     */
    convertToDestinationMatch_(serializedDefaultDestinationSelectionRulesStr) {
      let matchRules = null;
      try {
        if (serializedDefaultDestinationSelectionRulesStr) {
          matchRules =
              JSON.parse(serializedDefaultDestinationSelectionRulesStr);
        }
      } catch (e) {
        console.error('Failed to parse defaultDestinationSelectionRules: ' + e);
      }
      if (!matchRules) {
        return null;
      }

      const isLocal = !matchRules.kind || matchRules.kind == 'local';
      const isCloud = !matchRules.kind || matchRules.kind == 'cloud';
      if (!isLocal && !isCloud) {
        console.error('Unsupported type: "' + matchRules.kind + '"');
        return null;
      }

      const origins = [];
      if (isLocal) {
        origins.push(print_preview.DestinationOrigin.LOCAL);
        origins.push(print_preview.DestinationOrigin.PRIVET);
        origins.push(print_preview.DestinationOrigin.EXTENSION);
        origins.push(print_preview.DestinationOrigin.CROS);
      }
      if (isCloud) {
        origins.push(...print_preview.CloudOrigins);
      }

      let idRegExp = null;
      try {
        if (matchRules.idPattern) {
          idRegExp = new RegExp(matchRules.idPattern || '.*');
        }
      } catch (e) {
        console.error('Failed to parse regexp for "id": ' + e);
      }

      let displayNameRegExp = null;
      try {
        if (matchRules.namePattern) {
          displayNameRegExp = new RegExp(matchRules.namePattern || '.*');
        }
      } catch (e) {
        console.error('Failed to parse regexp for "name": ' + e);
      }

      return new print_preview.DestinationMatch(
          origins, idRegExp, displayNameRegExp,
          true /*skipVirtualDestinations*/);
    }

    /**
     * @return {print_preview.DestinationMatch} Creates rules matching
     *     previously selected destination.
     * @private
     */
    convertPreselectedToDestinationMatch_() {
      if (this.isDestinationValid_(this.selectedDestination_)) {
        return this.createExactDestinationMatch_(
            this.selectedDestination_.origin, this.selectedDestination_.id);
      }
      if (this.systemDefaultDestinationId_.length > 0) {
        return this.createExactDestinationMatch_(
            this.platformOrigin_, this.systemDefaultDestinationId_);
      }
      return null;
    }

    /**
     * @param {string | print_preview.DestinationOrigin} origin Destination
     *     origin.
     * @param {string} id Destination id.
     * @return {!print_preview.DestinationMatch} Creates rules matching
     *     provided destination.
     * @private
     */
    createExactDestinationMatch_(origin, id) {
      return new print_preview.DestinationMatch(
          [origin],
          new RegExp('^' + id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '$'),
          null /*displayNameRegExp*/, false /*skipVirtualDestinations*/);
    }

    /**
     * Updates the current active user account.
     * @param {string} activeUser
     */
    setActiveUser(activeUser) {
      this.activeUser_ = activeUser;
    }

    /**
     * Sets the destination store's Google Cloud Print interface.
     * @param {!cloudprint.CloudPrintInterface} cloudPrintInterface Interface
     *     to set.
     */
    setCloudPrintInterface(cloudPrintInterface) {
      assert(this.cloudPrintInterface_ == null);
      this.cloudPrintInterface_ = cloudPrintInterface;
      [cloudprint.CloudPrintInterfaceEventType.SEARCH_DONE,
       cloudprint.CloudPrintInterfaceEventType.SEARCH_FAILED,
      ].forEach(eventName => {
        this.tracker_.add(
            this.cloudPrintInterface_.getEventTarget(), eventName,
            this.onCloudPrintSearchDone_.bind(this));
      });
      this.tracker_.add(
          this.cloudPrintInterface_.getEventTarget(),
          cloudprint.CloudPrintInterfaceEventType.PRINTER_DONE,
          this.onCloudPrintPrinterDone_.bind(this));
      this.tracker_.add(
          this.cloudPrintInterface_.getEventTarget(),
          cloudprint.CloudPrintInterfaceEventType.PRINTER_FAILED,
          this.onCloudPrintPrinterFailed_.bind(this));
      this.tracker_.add(
          this.cloudPrintInterface_.getEventTarget(),
          cloudprint.CloudPrintInterfaceEventType.PROCESS_INVITE_DONE,
          this.onCloudPrintProcessInviteDone_.bind(this));
    }

    /**
     * @param {print_preview.Destination} destination Destination to select.
     */
    selectDestination(destination) {
      this.autoSelectMatchingDestination_ = null;
      // Clear the timeout. Otherwise, when it expires, we will fall back to the
      // default destination.
      if (this.autoSelectTimeout_) {
        clearTimeout(this.autoSelectTimeout_);
        this.autoSelectTimeout_ = null;
      } else if (destination == this.selectedDestination_) {
        return;
      }
      if (destination == null) {
        this.selectedDestination_ = null;
        this.dispatchEvent(
            new CustomEvent(DestinationStore.EventType.DESTINATION_SELECT));
        return;
      }

      assert(
          !destination.isProvisional,
          'Unable to select provisonal destinations');

      // Update and persist selected destination.
      this.selectedDestination_ = destination;
      // Adjust metrics.
      if (destination.cloudID &&
          this.destinations_.some(function(otherDestination) {
            return otherDestination.cloudID == destination.cloudID &&
                otherDestination != destination;
          })) {
        this.metrics_.record(
            destination.isPrivet ? print_preview.Metrics.DestinationSearchBucket
                                       .PRIVET_DUPLICATE_SELECTED :
                                   print_preview.Metrics.DestinationSearchBucket
                                       .CLOUD_DUPLICATE_SELECTED);
      }
      // Notify about selected destination change.
      this.dispatchEvent(
          new CustomEvent(DestinationStore.EventType.DESTINATION_SELECT));
      // Request destination capabilities from backend, since they are not
      // known yet.
      if (destination.capabilities == null) {
        const type = print_preview.originToType(destination.origin);
        if (type !== print_preview.PrinterType.CLOUD_PRINTER) {
          this.nativeLayer_.getPrinterCapabilities(destination.id, type)
              .then(
                  (caps) => this.onCapabilitiesSet_(
                      destination.origin, destination.id, caps),
                  () => this.onGetCapabilitiesFail_(
                      destination.origin, destination.id));
        } else {
          assert(
              this.cloudPrintInterface_ != null,
              'Cloud destination selected, but GCP is not enabled');
          this.cloudPrintInterface_.printer(
              destination.id, destination.origin, destination.account);
        }
      } else {
        this.sendSelectedDestinationUpdateEvent_();
      }
    }

    // <if expr="chromeos">
    /**
     * Attempt to resolve the capabilities for a Chrome OS printer.
     * @param {!print_preview.Destination} destination The destination which
     *     requires resolution.
     * @return {!Promise<!print_preview.PrinterSetupResponse>}
     */
    resolveCrosDestination(destination) {
      assert(destination.origin == print_preview.DestinationOrigin.CROS);
      return this.nativeLayer_.setupPrinter(destination.id);
    }

    /**
     * Attempts to resolve a provisional destination.
     * @param {!print_preview.Destination} destination Provisional destination
     *     that should be resolved.
     * @return {!Promise<?print_preview.Destination>}
     */
    resolveProvisionalDestination(destination) {
      assert(
          destination.provisionalType ==
              print_preview.DestinationProvisionalType.NEEDS_USB_PERMISSION,
          'Provisional type cannot be resolved.');
      return this.nativeLayer_.grantExtensionPrinterAccess(destination.id)
          .then(
              destinationInfo => {
                /**
                 * Removes the destination from the store and replaces it with a
                 * destination created from the resolved destination properties,
                 * if any are reported. Then returns the new destination.
                 */
                this.removeProvisionalDestination_(destination.id);
                const parsedDestination =
                    print_preview.parseExtensionDestination(destinationInfo);
                this.insertIntoStore_(parsedDestination);
                return parsedDestination;
              },
              () => {
                /**
                 * The provisional destination is removed from the store and
                 * null is returned.
                 */
                this.removeProvisionalDestination_(destination.id);
                return null;
              });
    }
    // </if>

    /**
     * Selects the Save as PDF fallback if it is available. If not, selects the
     * first destination if it exists. If the store is empty, starts loading all
     * printers to find one to select.
     * @private
     */
    selectFinalFallbackDestination_() {
      // Save as PDF should always exist if it is enabled.
      if (this.pdfPrinterEnabled_) {
        const saveToPdfKey = print_preview.createDestinationKey(
            print_preview.Destination.GooglePromotedId.SAVE_AS_PDF,
            print_preview.DestinationOrigin.LOCAL, '');
        this.selectDestination(assert(this.destinationMap_.get(saveToPdfKey)));
        return;
      }

      // Try selecting the first destination if there is at least one
      // destination already loaded.
      if (this.destinations_.length > 0) {
        this.selectDestination(this.destinations_[0]);
        return;
      }

      // Load all destinations to find one to select.
      this.selectFirstDestination_ = true;
      this.startLoadAllDestinations();
    }

    /**
     * Attempts to select system default destination with a fallback to the
     * 'Save to PDF' destination and a final fallback to the first destination
     * in the store.
     * @private
     */
    selectDefaultDestination_() {
      // Try the system default, if it isn't the destination that was
      // supposed to be autoselected and failed.
      if (this.autoSelectMatchingDestination_ &&
          !this.autoSelectMatchingDestination_.matchIdAndOrigin(
              this.systemDefaultDestinationId_, this.platformOrigin_) &&
          this.selectSystemDefaultDestination_()) {
        return;
      }

      this.selectFinalFallbackDestination_();
    }

    /**
     * Initiates loading of destinations.
     * @param{print_preview.PrinterType} type The type of destinations to load.
     * @private
     */
    startLoadDestinations_(type) {
      if (this.destinationSearchStatus_.get(type) ===
          print_preview.DestinationStorePrinterSearchStatus.DONE) {
        return;
      }
      this.destinationSearchStatus_.set(
          type, print_preview.DestinationStorePrinterSearchStatus.SEARCHING);
      this.nativeLayer_.getPrinters(type).then(
          this.onDestinationSearchDone_.bind(this, type), () => {
            // Will be rejected by C++ for privet printers if privet printing
            // is disabled.
            assert(type === print_preview.PrinterType.PRIVET_PRINTER);
            this.destinationSearchStatus_.set(
                type, print_preview.DestinationStorePrinterSearchStatus.DONE);
          });
    }

    /**
     * Requests load of COOKIE based cloud destinations for |account|.
     * @param {string} account
     */
    reloadUserCookieBasedDestinations(account) {
      const origins = this.loadedCloudOrigins_.get(account) || [];
      if (origins.includes(print_preview.DestinationOrigin.COOKIES)) {
        this.dispatchEvent(new CustomEvent(
            DestinationStore.EventType.DESTINATION_SEARCH_DONE));
      } else {
        this.startLoadCloudDestinations(
            print_preview.DestinationOrigin.COOKIES);
      }
    }

    /** Initiates loading of all known destination types. */
    startLoadAllDestinations() {
      // Printer types that need to be retrieved from the handler.
      const types = [
        print_preview.PrinterType.PRIVET_PRINTER,
        print_preview.PrinterType.EXTENSION_PRINTER,
        print_preview.PrinterType.LOCAL_PRINTER,
      ];

      // If the cloud printer handler is enabled, request cloud printers from
      // the handler instead of trying to directly communicate with the cloud
      // print server. See https://crbug.com/829414.
      if (loadTimeData.getBoolean('cloudPrinterHandlerEnabled')) {
        // Add cloud printer to the map.
        this.destinationSearchStatus_.set(
            print_preview.PrinterType.CLOUD_PRINTER,
            print_preview.DestinationStorePrinterSearchStatus.START);
        types.push(print_preview.PrinterType.CLOUD_PRINTER);
      } else {
        this.startLoadCloudDestinations();
      }

      for (const printerType of types) {
        this.startLoadDestinations_(printerType);
      }
    }

    /**
     * Initiates loading of cloud destinations.
     * @param {print_preview.DestinationOrigin=} opt_origin Search destinations
     *     for the specified origin only.
     */
    startLoadCloudDestinations(opt_origin) {
      if (this.cloudPrintInterface_ == null) {
        return;
      }

      const origins = this.loadedCloudOrigins_.get(this.activeUser_) || [];
      if (origins.length == 0 || (opt_origin && origins.includes(opt_origin))) {
        this.cloudPrintInterface_.search(this.activeUser_, opt_origin);
      }
    }

    /**
     * If a destination with key |key| is already in the store, selects it.
     * Otherwise, fetches the recent destination with key matching |key|.
     * @param {string} key
     * @param {!Array<!print_preview.RecentDestination>} recentDestinations
     * @return {boolean} Whether the destination was selected or fetch was
     *     started successfully.
     */
    selectRecentDestinationByKey(key, recentDestinations) {
      const destination = this.destinationMap_.get(key);
      if (destination) {
        this.selectDestination(destination);
        return true;
      }

      const recent = recentDestinations.find(d => {
        return print_preview.createRecentDestinationKey(d) === key;
      });
      if (recent) {
        return this.fetchPreselectedDestination_(recent);
      }

      // Should be fetching the Google Drive destination.
      const driveKey = print_preview.createDestinationKey(
          print_preview.Destination.GooglePromotedId.DOCS,
          print_preview.DestinationOrigin.COOKIES, this.activeUser_);
      assert(key === driveKey);
      return this.fetchPreselectedDestination_({
        id: print_preview.Destination.GooglePromotedId.DOCS,
        origin: print_preview.DestinationOrigin.COOKIES,
        account: this.activeUser_,
        capabilities: null,
        displayName: '',
        extensionId: '',
        extensionName: '',
      });
    }

    // <if expr="chromeos">
    /**
     * Removes the provisional destination with ID |provisionalId| from
     * |destinationMap_| and |destinations_|.
     * @param{string} provisionalId The provisional destination ID.
     * @private
     */
    removeProvisionalDestination_(provisionalId) {
      this.destinations_ = this.destinations_.filter(
          function(el) {
            if (el.id == provisionalId) {
              this.destinationMap_.delete(el.key);
              return false;
            }
            return true;
          }, this);
    }
    // </if>

    /**
     * Inserts {@code destination} to the data store and dispatches a
     * DESTINATIONS_INSERTED event.
     * @param {!print_preview.Destination} destination Print destination to
     *     insert.
     * @private
     */
    insertDestination_(destination) {
      if (this.insertIntoStore_(destination)) {
        this.destinationsInserted_(destination);
      }
    }

    /**
     * Inserts multiple {@code destinations} to the data store and dispatches
     * single DESTINATIONS_INSERTED event.
     * @param {!Array<!print_preview.Destination |
     *                !Array<print_preview.Destination>>} destinations Print
     *     destinations to insert.
     * @private
     */
    insertDestinations_(destinations) {
      let inserted = false;
      destinations.forEach(destination => {
        if (Array.isArray(destination)) {
          // privet printers return arrays of 1 or 2 printers
          inserted = destination.reduce(function(soFar, d) {
            return this.insertIntoStore_(d) || soFar;
          }, inserted);
        } else {
          inserted = this.insertIntoStore_(destination) || inserted;
        }
      });
      if (inserted) {
        this.destinationsInserted_();
      }
    }

    /**
     * Dispatches DESTINATIONS_INSERTED event. In auto select mode, tries to
     * update selected destination to match
     * {@code autoSelectMatchingDestination_}.
     * @param {print_preview.Destination=} opt_destination The only destination
     *     that was changed or skipped if possibly more than one destination was
     *     changed. Used as a hint to limit destination search scope against
     *     {@code autoSelectMatchingDestination_}.
     */
    destinationsInserted_(opt_destination) {
      this.dispatchEvent(
          new CustomEvent(DestinationStore.EventType.DESTINATIONS_INSERTED));
      if (this.autoSelectMatchingDestination_) {
        const destinationsToSearch =
            opt_destination && [opt_destination] || this.destinations_;
        destinationsToSearch.some(function(destination) {
          if (this.autoSelectMatchingDestination_.match(destination)) {
            this.selectDestination(destination);
            return true;
          }
        }, this);
      }
    }

    /**
     * Sends SELECTED_DESTINATION_CAPABILITIES_READY event if the destination
     * is supported, or ERROR otherwise of with error type UNSUPPORTED.
     * @private
     */
    sendSelectedDestinationUpdateEvent_() {
      if (this.selectedDestination_.shouldShowInvalidCertificateError) {
        this.dispatchEvent(new CustomEvent(
            DestinationStore.EventType.ERROR,
            {detail: print_preview.DestinationErrorType.UNSUPPORTED}));
      } else {
        this.dispatchEvent(
            new CustomEvent(DestinationStore.EventType
                                .SELECTED_DESTINATION_CAPABILITIES_READY));
      }
    }

    /**
     * Updates an existing print destination with capabilities and display name
     * information. If the destination doesn't already exist, it will be added.
     * @param {!print_preview.Destination} destination Destination to update.
     * @private
     */
    updateDestination_(destination) {
      assert(destination.constructor !== Array, 'Single printer expected');
      destination.capabilities =
          localizeCapabilities(assert(destination.capabilities));
      if (print_preview.originToType(destination.origin) !==
          print_preview.PrinterType.LOCAL_PRINTER) {
        destination.capabilities = sortMediaSizes(destination.capabilities);
      }
      const existingDestination = this.destinationMap_.get(destination.key);
      if (existingDestination != undefined) {
        existingDestination.capabilities = destination.capabilities;
      } else {
        this.insertDestination_(destination);
      }

      if (this.selectedDestination_ &&
          (existingDestination == this.selectedDestination_ ||
           destination == this.selectedDestination_)) {
        this.sendSelectedDestinationUpdateEvent_();
      }
    }

    /**
     * Called when loading of extension managed printers is done.
     * @private
     */
    endExtensionPrinterSearch_() {
      // Clear initially selected (cached) extension destination if it hasn't
      // been found among reported extension destinations.
      if (this.autoSelectMatchingDestination_ &&
          this.autoSelectMatchingDestination_.matchOrigin(
              print_preview.DestinationOrigin.EXTENSION) &&
          this.selectedDestination_ && this.selectedDestination_.isExtension) {
        this.selectDefaultDestination_();
      }
    }

    /**
     * Inserts a destination into the store without dispatching any events.
     * @param {!print_preview.Destination} destination The destination to be
     *     inserted.
     * @return {boolean} Whether the inserted destination was not already in the
     *     store.
     * @private
     */
    insertIntoStore_(destination) {
      const key = destination.key;
      const existingDestination = this.destinationMap_.get(key);
      if (existingDestination == undefined) {
        this.destinations_.push(destination);
        this.destinationMap_.set(key, destination);
        return true;
      }
      if (existingDestination.connectionStatus ==
              print_preview.DestinationConnectionStatus.UNKNOWN &&
          destination.connectionStatus !=
              print_preview.DestinationConnectionStatus.UNKNOWN) {
        existingDestination.connectionStatus = destination.connectionStatus;
        return true;
      }
      return false;
    }

    /**
     * Creates a local PDF print destination.
     * @private
     */
    createLocalPdfPrintDestination_() {
      // TODO(alekseys): Create PDF printer in the native code and send its
      // capabilities back with other local printers.
      if (this.pdfPrinterEnabled_) {
        this.insertDestination_(new print_preview.Destination(
            print_preview.Destination.GooglePromotedId.SAVE_AS_PDF,
            print_preview.DestinationType.LOCAL,
            print_preview.DestinationOrigin.LOCAL,
            loadTimeData.getString('printToPDF'),
            print_preview.DestinationConnectionStatus.ONLINE));
      }
    }

    /**
     * Starts a timeout to select the default destination.
     * @private
     */
    startAutoSelectTimeout_() {
      clearTimeout(this.autoSelectTimeout_);
      this.autoSelectTimeout_ = setTimeout(
          this.selectDefaultDestination_.bind(this),
          DestinationStore.AUTO_SELECT_TIMEOUT_);
    }

    /**
     * Resets the state of the destination store to its initial state.
     * @private
     */
    reset_() {
      this.destinations_ = [];
      this.destinationMap_.clear();
      this.inFlightCloudPrintRequests_.clear();
      this.loadedCloudOrigins_.clear();
      this.destinationSearchStatus_.forEach((status, type) => {
        this.destinationSearchStatus_.set(
            type, print_preview.DestinationStorePrinterSearchStatus.START);
      });
      this.startAutoSelectTimeout_();
      this.dispatchEvent(
          new CustomEvent(DestinationStore.EventType.DESTINATIONS_RESET));
    }

    /**
     * Called when destination search is complete for some type of printer.
     * @param {!print_preview.PrinterType} type The type of printers that are
     *     done being retreived.
     */
    onDestinationSearchDone_(type) {
      this.destinationSearchStatus_.set(
          type, print_preview.DestinationStorePrinterSearchStatus.DONE);
      this.dispatchEvent(
          new CustomEvent(DestinationStore.EventType.DESTINATION_SEARCH_DONE));
      if (type === print_preview.PrinterType.EXTENSION_PRINTER) {
        this.endExtensionPrinterSearch_();
      }
      this.sendNoPrinterEventIfNeeded_();
    }

    /**
     * Called when the native layer retrieves the capabilities for the selected
     * local destination. Updates the destination with new capabilities if the
     * destination already exists, otherwise it creates a new destination and
     * then updates its capabilities.
     * @param {!print_preview.DestinationOrigin} origin The origin of the
     *     print destination.
     * @param {string} id The id of the print destination.
     * @param {!print_preview.CapabilitiesResponse} settingsInfo Contains
     *     the capabilities of the print destination, and information about
     *     the destination except in the case of extension printers.
     * @private
     */
    onCapabilitiesSet_(origin, id, settingsInfo) {
      let dest = null;
      if (origin !== print_preview.DestinationOrigin.PRIVET) {
        const key = print_preview.createDestinationKey(id, origin, '');
        dest = this.destinationMap_.get(key);
      }
      if (!dest) {
        // Ignore unrecognized extension printers
        if (!settingsInfo.printer) {
          assert(origin === print_preview.DestinationOrigin.EXTENSION);
          return;
        }
        dest = /** @type {!print_preview.Destination} */ (
            print_preview.parseDestination(
                print_preview.originToType(origin),
                assert(settingsInfo.printer)));
      }
      if (dest) {
        if ((origin === print_preview.DestinationOrigin.LOCAL ||
             origin === print_preview.DestinationOrigin.CROS) &&
            dest.capabilities) {
          // If capabilities are already set for this destination ignore new
          // results. This prevents custom margins from being cleared as long
          // as the user does not change to a new non-recent destination.
          return;
        }
        dest.capabilities = settingsInfo.capabilities;
        this.updateDestination_(dest);
      }
    }

    /**
     * Called when a request to get a local destination's print capabilities
     * fails. If the destination is the initial destination, auto-select another
     * destination instead.
     * @param {print_preview.DestinationOrigin} origin The origin type of the
     *     failed destination.
     * @param {string} destinationId The destination ID that failed.
     * @private
     */
    onGetCapabilitiesFail_(origin, destinationId) {
      console.warn(
          'Failed to get print capabilities for printer ' + destinationId);
      if (this.selectedDestination_ &&
          this.selectedDestination_.id == destinationId) {
        this.dispatchEvent(new CustomEvent(
            DestinationStore.EventType.ERROR,
            {detail: print_preview.DestinationErrorType.INVALID}));
      }
      if (this.autoSelectMatchingDestination_ &&
          this.autoSelectMatchingDestination_.matchIdAndOrigin(
              destinationId, origin)) {
        this.selectDefaultDestination_();
      }
    }

    /**
     * Called when the /search call completes, either successfully or not.
     * In case of success, stores fetched destinations.
     * @param {!CustomEvent<!cloudprint.CloudPrintInterfaceSearchDoneDetail>}
     *      event Contains the request result.
     * @private
     */
    onCloudPrintSearchDone_(event) {
      const payload = event.detail;
      if (payload.printers && payload.printers.length > 0) {
        this.insertDestinations_(payload.printers);
        if (this.selectFirstDestination_) {
          this.selectDestination(this.destinations_[0]);
          this.selectFirstDestination_ = false;
        }
      }
      if (payload.searchDone) {
        const origins = this.loadedCloudOrigins_.get(payload.user) || [];
        if (origins.includes(payload.origin)) {
          this.loadedCloudOrigins_.set(
              payload.user, origins.concat([payload.origin]));
        }
      }
      this.dispatchEvent(
          new CustomEvent(DestinationStore.EventType.DESTINATION_SEARCH_DONE));
      this.sendNoPrinterEventIfNeeded_();
    }

    /**
     * Checks if the search is done and no printers are found. If so, fires a
     * DestinationStore.EventType.ERROR event with error type NO_DESTINATIONS.
     * @private
     */
    sendNoPrinterEventIfNeeded_() {
      const isLocalDestinationSearchNotStarted =
          Array.from(this.destinationSearchStatus_.values())
              .some(
                  el => el ===
                      print_preview.DestinationStorePrinterSearchStatus.START);
      if (isLocalDestinationSearchNotStarted ||
          this.isPrintDestinationSearchInProgress ||
          !this.selectFirstDestination_) {
        return;
      }
      // <if expr="chromeos">
      this.selectFirstDestination_ = false;
      this.dispatchEvent(new CustomEvent(
          DestinationStore.EventType.ERROR,
          {detail: print_preview.DestinationErrorType.NO_DESTINATIONS}));
      // </if>
    }

    /**
     * Called when /printer call completes. Updates the specified destination's
     * print capabilities.
     * @param {!CustomEvent<!print_preview.Destination>} event Contains
     *     detailed information about the destination.
     * @private
     */
    onCloudPrintPrinterDone_(event) {
      this.updateDestination_(event.detail);
      this.inFlightCloudPrintRequests_.delete(event.detail.key);
    }

    /**
     * Called when the Google Cloud Print interface fails to lookup a
     * destination. Selects another destination if the failed destination was
     * the initial destination.
     * @param {!CustomEvent<!cloudprint.CloudPrintInterfacePrinterFailedDetail>}
     *     event Contains the ID of the destination that failed to be looked up.
     * @private
     */
    onCloudPrintPrinterFailed_(event) {
      const key = print_preview.createDestinationKey(
          event.detail.destinationId, event.detail.origin, this.activeUser_);
      this.inFlightCloudPrintRequests_.delete(key);
      if (this.autoSelectMatchingDestination_ &&
          this.autoSelectMatchingDestination_.matchIdAndOrigin(
              event.detail.destinationId, event.detail.origin)) {
        console.warn(
            'Failed to fetch last used printer caps: ' +
            event.detail.destinationId);
        this.selectDefaultDestination_();
      } else {
        // Log the failure
        console.warn(
            'Failed to fetch printer capabilities for ' +
            event.detail.destinationId + ' with origin ' + event.detail.origin);
      }
    }

    /**
     * Called when printer sharing invitation was processed successfully.
     * @param {!CustomEvent<!cloudprint.CloudPrintInterfaceProcessInviteDetail>}
     *     event Contains detailed information about the invite and newly
     *     accepted destination (if known).
     * @private
     */
    onCloudPrintProcessInviteDone_(event) {
      if (event.detail.accept && event.detail.printer) {
        this.insertDestination_(event.detail.printer);
      }
    }

    /**
     * Called when a printer or printers are detected after sending getPrinters
     * from the native layer.
     * @param {print_preview.PrinterType} type The type of printer(s) added.
     * @param {!Array<!print_preview.LocalDestinationInfo |
     *                !print_preview.PrivetPrinterDescription |
     *                !print_preview.ProvisionalDestinationInfo>} printers
     *     Information about the printers that have been retrieved.
     */
    onPrintersAdded_(type, printers) {
      this.insertDestinations_(printers.map(
          printer => /** @type {!print_preview.Destination} */ (
              print_preview.parseDestination(type, printer))));

      if (this.selectFirstDestination_) {
        this.selectDestination(this.destinations_[0]);
        this.selectFirstDestination_ = false;
      }
    }

    /**
     * Called from print preview after the user was requested to sign in, and
     * did so successfully.
     */
    onDestinationsReload() {
      this.reset_();
      this.autoSelectMatchingDestination_ =
          this.convertPreselectedToDestinationMatch_();
      this.createLocalPdfPrintDestination_();
      this.startLoadAllDestinations();
    }
  }

  /**
   * Event types dispatched by the destination store.
   * @enum {string}
   */
  DestinationStore.EventType = {
    DESTINATION_SEARCH_DONE:
        'print_preview.DestinationStore.DESTINATION_SEARCH_DONE',
    DESTINATION_SELECT: 'print_preview.DestinationStore.DESTINATION_SELECT',
    DESTINATIONS_INSERTED:
        'print_preview.DestinationStore.DESTINATIONS_INSERTED',
    DESTINATIONS_RESET: 'print_preview.DestinationStore.DESTINATIONS_RESET',
    ERROR: 'print_preview.DestinationStore.ERROR',
    SELECTED_DESTINATION_CAPABILITIES_READY: 'print_preview.DestinationStore' +
        '.SELECTED_DESTINATION_CAPABILITIES_READY',
  };

  /**
   * Delay in milliseconds before the destination store ignores the initial
   * destination ID and just selects any printer (since the initial destination
   * was not found).
   * @private {number}
   * @const
   */
  DestinationStore.AUTO_SELECT_TIMEOUT_ = 15000;

  /**
   * Maximum amount of time spent searching for extension destinations, in
   * milliseconds.
   * @private {number}
   * @const
   */
  DestinationStore.EXTENSION_SEARCH_DURATION_ = 5000;

  /**
   * Human readable names for media sizes in the cloud print CDD.
   * https://developers.google.com/cloud-print/docs/cdd
   * @private {Object<string>}
   * @const
   */
  DestinationStore.MEDIA_DISPLAY_NAMES_ = {
    'ISO_2A0': '2A0',
    'ISO_A0': 'A0',
    'ISO_A0X3': 'A0x3',
    'ISO_A1': 'A1',
    'ISO_A10': 'A10',
    'ISO_A1X3': 'A1x3',
    'ISO_A1X4': 'A1x4',
    'ISO_A2': 'A2',
    'ISO_A2X3': 'A2x3',
    'ISO_A2X4': 'A2x4',
    'ISO_A2X5': 'A2x5',
    'ISO_A3': 'A3',
    'ISO_A3X3': 'A3x3',
    'ISO_A3X4': 'A3x4',
    'ISO_A3X5': 'A3x5',
    'ISO_A3X6': 'A3x6',
    'ISO_A3X7': 'A3x7',
    'ISO_A3_EXTRA': 'A3 Extra',
    'ISO_A4': 'A4',
    'ISO_A4X3': 'A4x3',
    'ISO_A4X4': 'A4x4',
    'ISO_A4X5': 'A4x5',
    'ISO_A4X6': 'A4x6',
    'ISO_A4X7': 'A4x7',
    'ISO_A4X8': 'A4x8',
    'ISO_A4X9': 'A4x9',
    'ISO_A4_EXTRA': 'A4 Extra',
    'ISO_A4_TAB': 'A4 Tab',
    'ISO_A5': 'A5',
    'ISO_A5_EXTRA': 'A5 Extra',
    'ISO_A6': 'A6',
    'ISO_A7': 'A7',
    'ISO_A8': 'A8',
    'ISO_A9': 'A9',
    'ISO_B0': 'B0',
    'ISO_B1': 'B1',
    'ISO_B10': 'B10',
    'ISO_B2': 'B2',
    'ISO_B3': 'B3',
    'ISO_B4': 'B4',
    'ISO_B5': 'B5',
    'ISO_B5_EXTRA': 'B5 Extra',
    'ISO_B6': 'B6',
    'ISO_B6C4': 'B6C4',
    'ISO_B7': 'B7',
    'ISO_B8': 'B8',
    'ISO_B9': 'B9',
    'ISO_C0': 'C0',
    'ISO_C1': 'C1',
    'ISO_C10': 'C10',
    'ISO_C2': 'C2',
    'ISO_C3': 'C3',
    'ISO_C4': 'C4',
    'ISO_C5': 'C5',
    'ISO_C6': 'C6',
    'ISO_C6C5': 'C6C5',
    'ISO_C7': 'C7',
    'ISO_C7C6': 'C7C6',
    'ISO_C8': 'C8',
    'ISO_C9': 'C9',
    'ISO_DL': 'Envelope DL',
    'ISO_RA0': 'RA0',
    'ISO_RA1': 'RA1',
    'ISO_RA2': 'RA2',
    'ISO_SRA0': 'SRA0',
    'ISO_SRA1': 'SRA1',
    'ISO_SRA2': 'SRA2',
    'JIS_B0': 'B0 (JIS)',
    'JIS_B1': 'B1 (JIS)',
    'JIS_B10': 'B10 (JIS)',
    'JIS_B2': 'B2 (JIS)',
    'JIS_B3': 'B3 (JIS)',
    'JIS_B4': 'B4 (JIS)',
    'JIS_B5': 'B5 (JIS)',
    'JIS_B6': 'B6 (JIS)',
    'JIS_B7': 'B7 (JIS)',
    'JIS_B8': 'B8 (JIS)',
    'JIS_B9': 'B9 (JIS)',
    'JIS_EXEC': 'Executive (JIS)',
    'JPN_CHOU2': 'Choukei 2',
    'JPN_CHOU3': 'Choukei 3',
    'JPN_CHOU4': 'Choukei 4',
    'JPN_HAGAKI': 'Hagaki',
    'JPN_KAHU': 'Kahu Envelope',
    'JPN_KAKU2': 'Kaku 2',
    'JPN_OUFUKU': 'Oufuku Hagaki',
    'JPN_YOU4': 'You 4',
    'NA_10X11': '10x11',
    'NA_10X13': '10x13',
    'NA_10X14': '10x14',
    'NA_10X15': '10x15',
    'NA_11X12': '11x12',
    'NA_11X15': '11x15',
    'NA_12X19': '12x19',
    'NA_5X7': '5x7',
    'NA_6X9': '6x9',
    'NA_7X9': '7x9',
    'NA_9X11': '9x11',
    'NA_A2': 'A2',
    'NA_ARCH_A': 'Arch A',
    'NA_ARCH_B': 'Arch B',
    'NA_ARCH_C': 'Arch C',
    'NA_ARCH_D': 'Arch D',
    'NA_ARCH_E': 'Arch E',
    'NA_ASME_F': 'ASME F',
    'NA_B_PLUS': 'B-plus',
    'NA_C': 'C',
    'NA_C5': 'C5',
    'NA_D': 'D',
    'NA_E': 'E',
    'NA_EDP': 'EDP',
    'NA_EUR_EDP': 'European EDP',
    'NA_EXECUTIVE': 'Executive',
    'NA_F': 'F',
    'NA_FANFOLD_EUR': 'FanFold European',
    'NA_FANFOLD_US': 'FanFold US',
    'NA_FOOLSCAP': 'FanFold German Legal',
    'NA_GOVT_LEGAL': 'Government Legal',
    'NA_GOVT_LETTER': 'Government Letter',
    'NA_INDEX_3X5': 'Index 3x5',
    'NA_INDEX_4X6': 'Index 4x6',
    'NA_INDEX_4X6_EXT': 'Index 4x6 ext',
    'NA_INDEX_5X8': '5x8',
    'NA_INVOICE': 'Invoice',
    'NA_LEDGER': 'Tabloid',  // Ledger in portrait is called Tabloid.
    'NA_LEGAL': 'Legal',
    'NA_LEGAL_EXTRA': 'Legal extra',
    'NA_LETTER': 'Letter',
    'NA_LETTER_EXTRA': 'Letter extra',
    'NA_LETTER_PLUS': 'Letter plus',
    'NA_MONARCH': 'Monarch',
    'NA_NUMBER_10': 'Envelope #10',
    'NA_NUMBER_11': 'Envelope #11',
    'NA_NUMBER_12': 'Envelope #12',
    'NA_NUMBER_14': 'Envelope #14',
    'NA_NUMBER_9': 'Envelope #9',
    'NA_PERSONAL': 'Personal',
    'NA_QUARTO': 'Quarto',
    'NA_SUPER_A': 'Super A',
    'NA_SUPER_B': 'Super B',
    'NA_WIDE_FORMAT': 'Wide format',
    'OM_DAI_PA_KAI': 'Dai-pa-kai',
    'OM_FOLIO': 'Folio',
    'OM_FOLIO_SP': 'Folio SP',
    'OM_INVITE': 'Invite Envelope',
    'OM_ITALIAN': 'Italian Envelope',
    'OM_JUURO_KU_KAI': 'Juuro-ku-kai',
    'OM_LARGE_PHOTO': 'Large photo',
    'OM_OFICIO': 'Oficio',
    'OM_PA_KAI': 'Pa-kai',
    'OM_POSTFIX': 'Postfix Envelope',
    'OM_SMALL_PHOTO': 'Small photo',
    'PRC_1': 'prc1 Envelope',
    'PRC_10': 'prc10 Envelope',
    'PRC_16K': 'prc 16k',
    'PRC_2': 'prc2 Envelope',
    'PRC_3': 'prc3 Envelope',
    'PRC_32K': 'prc 32k',
    'PRC_4': 'prc4 Envelope',
    'PRC_5': 'prc5 Envelope',
    'PRC_6': 'prc6 Envelope',
    'PRC_7': 'prc7 Envelope',
    'PRC_8': 'prc8 Envelope',
    'ROC_16K': 'ROC 16K',
    'ROC_8K': 'ROC 8k',
  };

  // Export
  return {DestinationStore: DestinationStore};
});
